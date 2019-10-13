//
// Botnet server for TSAM 2019
//
// Command line: ./tsamvgroup88
//
// Authors: Hjörtur Jóhann V. & Ívar Kristinn H.
//
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <vector>
#include <iostream>
#include <sstream>
#include <thread>
#include <map>
#include <unistd.h>

const int MAX_SERVER_CONNECTIONS = 5;
const int CLIENT_PORT = 4053;
int SERVER_PORT;
std::string SERVER_HOST = "130.208.243.61";
std::string SERVER_ID = "P3_GROUP_88";
struct timeval waitTime = {60};
time_t keepAliveCountdown = time(0);

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
class Client {
  public:
    int sock;              // socket of client connection
    std::string name;           // Limit length of name of client's user

    Client(int socket) : sock(socket) {} 

    ~Client(){}            // Virtual destructor defined for base class
};

struct Server {
    int sock;
    std::string host;
    int port;
    std::string group_id;
    bool hasReceivedOurServerList = false;
    time_t timeSinceLastKeepAlive = time(0);

    Server(int _sock, std::string _host, int _port) {
        sock = _sock;
        host = _host;
        port = _port;
    }

    ~Server(){}
};
// Note: map is not necessarily the most efficient method to use here,
// especially for a server with large numbers of simulataneous connections,
// where performance is also expected to be an issue.
//
// Quite often a simple array can be used as a lookup table, 
// (indexed on socket no.) sacrificing memory for speed.

std::map<int, Client*> clients; // Lookup table for per Client information
std::map<int, Server*> servers; // Lookup table for per Server information
std::map<std::string, std::vector<std::string>> outgoingMessages; // Other servers messages
std::map<std::string, std::vector<std::string>> incomingMessages; // Our messages

// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.

/* Given a string split it on the delimiter given */
std::vector<std::string> stringSplit(std::string str, std::string delim) {
    std::vector<std::string> tokens;
    std::string orig = str;

    size_t pos = 0;
    std::string token;
    while ((pos = str.find(delim)) != std::string::npos) {
        token = str.substr(0, pos);
        tokens.push_back(token);
        str.erase(0, pos + delim.length());
    }
    tokens.push_back(str);
    
    if(tokens.size() == 1) {
        std::vector<std::string> stuff;
        stuff.push_back(orig.substr(0, orig.length()-1));
        return stuff;
    }
    
    return tokens;
}

int open_socket(int portno) {
   struct sockaddr_in sk_addr;   // address settings for bind()
   int sock;                     // socket opened for this port
   int set = 1;                  // for setsockopt

   // Create socket for connection. Set to be non-blocking, so recv will
   // return immediately if there isn't anything waiting to be read.
   if((sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0) {
    perror("Failed to open socket");
    return(-1);
   }

   // Turn on SO_REUSEADDR to allow socket to be quickly reused after 
   // program exit.
   if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0) {
      perror("Failed to set SO_REUSEADDR:");
   }
   set = 1;
   memset(&sk_addr, 0, sizeof(sk_addr));

   sk_addr.sin_family      = AF_INET;
   sk_addr.sin_addr.s_addr = INADDR_ANY;
   sk_addr.sin_port        = htons(portno);

   // Bind to socket to listen for connections from clients

   if(bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0) {
      perror("Failed to bind to socket:");
      return(-1);
   }
   else {
        if(listen(sock, MAX_SERVER_CONNECTIONS) < 0) {
            printf("Listen failed!");
            exit(0);
        }
        return(sock);
   }
}

// Close a client's connection, remove it from the client list, and
// tidy up select sockets afterwards.
void closeClient(int clientSocket, fd_set *openSockets, int *maxfds) {
     // Remove client from the clients list
     clients.erase(clientSocket);

     // And remove from the list of open sockets.
     FD_CLR(clientSocket, openSockets);
}

/* Get address with specified hostname and port */
sockaddr_in get_sockaddr_in(const char *hostname, int port) {
    struct hostent *server;
    struct sockaddr_in serv_addr;

    server = gethostbyname(hostname);
    if (server == NULL) {
        std::cout << "Error resolving host" << std::endl;
        return serv_addr;
    }

    serv_addr.sin_family = AF_INET;
    memcpy((char *)&serv_addr.sin_addr.s_addr,
           (char *)server->h_addr,
           server->h_length);
    serv_addr.sin_port = htons(port);

    return serv_addr;
}

void connect_to_server(std::string host, int port, fd_set &openSockets) {
    int sock;
    struct sockaddr_in serv_addr;
    int set = 1; // Toggle for setsockopt
    char addr[INET_ADDRSTRLEN];
    
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    serv_addr = get_sockaddr_in(host.c_str(), port);

    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
        std::cout << "setsockopt failed" << std::endl;

    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0){
        std::cout << "Server: Connection to host " + host + " on port " + std::to_string(port) + " successful" << std::endl;
        // Add it to the active set
        FD_SET(sock, &openSockets);
        // Add server to server table
        servers[sock] = new Server(sock, host, port);
    }
}

std::string get_server_list() {
    std::string response = "SERVERS," + SERVER_ID + "," + SERVER_HOST + "," + std::to_string(SERVER_PORT) + ";";
    for(auto const& server : servers) {
        response += server.second->group_id + "," + server.second->host + "," + std::to_string(server.second->port) + ";";
    }

    return response;
}

void parse_client_command(int clientSock, char* buffer, fd_set &openSockets) {
    std::vector<std::string> tokens = stringSplit(buffer, ",");
    //Remove the character at the end of the last token so it matches our commands
    if(tokens.size() != 1) {
        tokens[tokens.size()-1] = tokens[tokens.size()-1].substr(0, tokens[tokens.size()-1].length()-1);
    }
    
    //std::cout << buffer << " token len: " << tokens[0].length() << " token size: " << tokens.size() << " BOOL: " << tokens[0].compare("LISTSERVERS") << std::endl;

    
    if((tokens[0].compare("CONNECT") == 0) && (tokens.size() == 3)) {
        //tokens[1] is the host
        //tokens[2] is the port
        connect_to_server(tokens[1], atoi(tokens[2].c_str()), openSockets);
    }
    else if(tokens[0].compare("GETMSG") == 0 && (tokens.size() == 2)) {
        //tokens[1] is the group id of the server we want a message from
        //incomingMessages[tokens[1]].push_back("SUp my dudes");
        std::cout << "Tokens 1: " << tokens[1] << std::endl;
        std::cout << "Size of inc messages with this group id: " << incomingMessages[tokens[1]].size() << std::endl;
        if(incomingMessages[tokens[1]].size() != 0) {
            std::string newString = "Messages from server " + tokens[1];
            write(clientSock, newString.c_str(), newString.size());
            usleep(500);
            for(auto const& inc : incomingMessages[tokens[1]]) {
                write(clientSock, inc.c_str(), inc.size());
                usleep(500);
            }
            incomingMessages.erase(tokens[1]);
            //std::cout << incomingMessages[tokens[1]].size() << std::endl;
        } else {
            std::string noMessages = "No messages from server";
            write(clientSock, noMessages.c_str(), noMessages.size());
        }

    }
    else if(tokens[0].compare("SENDMSG") == 0) {
        std::cout << "Inside sendmsg client command" << std::endl;
        std::string message;
        for(int i = 2; i < tokens.size(); i++) {
            message += tokens[i] + ",";
        }
        message = message.substr(0, message.length()-1);

        if(tokens[1] == SERVER_ID) {
            std::cout << "Adding to our messages!" << std::endl;
            incomingMessages[SERVER_ID].push_back(message);
            std::cout << "New size of our inc messages: " << incomingMessages[SERVER_ID].size() << std::endl;
            std::cout << incomingMessages[SERVER_ID][0] << std::endl;
        }
        else {
            bool found = false;
            for(auto const& server : servers) {
                if(server.second->group_id == tokens[1]) {
                    found = true;
                    write(server.second->sock, message.c_str(), message.size());
                    break;
                }
            }
            if(!found) {
                outgoingMessages[tokens[1]].push_back(message);
            }
        }
    }
    else if((tokens[0].compare("LISTSERVERS") == 0) && (tokens.size() == 1)) {
        std::string serverList = get_server_list();
        write(clientSock, serverList.c_str(), serverList.size());
    }
    else {
        std::cout << "Unknown command from client:" << buffer << std::endl;
    }
}

//maybe bool to be consistent with recv form client
// Process command from server on the server
void parse_server_command(int serverSock, fd_set &openSockets, const char *buffer) {
    std::vector<std::string> tokens = stringSplit(buffer, ",");
    
    std::cout << "ServerCommand tokens: " << tokens[0] << std::endl;
    
    if((tokens[0].compare("SERVERS") == 0)) {
        servers[serverSock]->group_id = tokens[1];
        std::cout << buffer << std::endl;
    }
    else if((tokens[0].compare("LISTSERVERS") == 0) && (tokens.size() == 2)) {
        servers[serverSock]->group_id = tokens[1];
        std::string serverList = get_server_list();
        std::string stuffedList = "\x01" + serverList + "\x04";
        write(serverSock, stuffedList.c_str(), stuffedList.size());
        usleep(500);
        //send listservers command back
        if(!servers[serverSock]->hasReceivedOurServerList) {
            std::string list_servers_command = "\x01LISTSERVERS," + SERVER_ID + "\x04";
            write(serverSock, list_servers_command.c_str(), list_servers_command.length());
            servers[serverSock]->hasReceivedOurServerList = true;
        }

    }
    else if(tokens[0].compare("KEEPALIVE") == 0 && (tokens.size() == 2)) {
        //update this servers keepalive time
        std::cout << "We got a keepalive message from: " <<  servers[serverSock]->group_id << std::endl;
        servers[serverSock]->timeSinceLastKeepAlive = time(0);
    }
    else if(tokens[0].compare("GET_MSG") == 0 && (tokens.size() == 2)) {
        //Bruh, request messages from tokens[1] server

        //tokens[1] is the group id of the server we want a message from
        //incomingMessages[tokens[1]].push_back("SUp my dudes");
        std::cout << "Tokens 1: " << tokens[1] << std::endl;
        std::cout << "Size of inc messages with this group id: " << outgoingMessages[tokens[1]].size() << std::endl;
        if(outgoingMessages[tokens[1]].size() != 0) {
            for(auto const& out : outgoingMessages[tokens[1]]) {
                std::string formattedString = "\x01SEND_MSG," + SERVER_ID + "," + tokens[1] + "," + out + "\x04";
                write(serverSock, formattedString.c_str(), formattedString.size());
                usleep(500);
            }
            outgoingMessages.erase(tokens[1]);
            //std::cout << incomingMessages[tokens[1]].size() << std::endl;
        } else {
            //Do nothing I think
        }

    }
    else if(tokens[0].compare("SEND_MSG") == 0) {
        //Tokens[0] = SEND MSG
        //Tokens[1] = FROM_GROUP_ID
        //Tokens[2] = TO_GROUP_ID
        std::string message;
        for(int i = 3; i < tokens.size(); i++) {
            message += tokens[i] + ",";
        }
        message = message.substr(0, message.length()-1);

        if(tokens[2] == SERVER_ID) {
            std::cout << "Adding to our messages!" << std::endl;
            incomingMessages[SERVER_ID].push_back(message);
            std::cout << "New size of our inc messages: " << incomingMessages[SERVER_ID].size() << std::endl;
            std::cout << incomingMessages[SERVER_ID][0] << std::endl;
        }
        else {
            bool found = false;
            for(auto const& server : servers) {
                if(server.second->group_id == tokens[2]) {
                    found = true;
                    //Add the new message we got in this command to the list of message we have stored for the server
                    //with this group id (stores in tokens[2])
                    outgoingMessages[tokens[2]].push_back(message);
                    for(auto const& out : outgoingMessages[tokens[2]]) {
                        std::string formattedString = "\x01SEND_MSG," + tokens[1] + "," + tokens[2] + "," + out + "\x04";
                        write(server.second->sock, formattedString.c_str(), formattedString.size());
                        usleep(500);
                    }
                    break;
                }
            }
            if(!found) {
                outgoingMessages[tokens[2]].push_back(message);
            }
        }
    }
    else if(tokens[0].compare("LEAVE") == 0) {
        close(serverSock);
        FD_CLR(serverSock, &openSockets);
        servers.erase(serverSock);
    }
    else if(tokens[0].compare("STATUSREQ") == 0) {
        
    }
    else {
        std::cout << "Unknown command from server: " << tokens[0] << std::endl;
    }
}

//Handle commands coming from connected clients
void client_commands(fd_set &openSockets, fd_set &readSockets) {
    for(auto const& client : clients) {
        int sock = client.second->sock;
        if(FD_ISSET(sock, &readSockets)) {
            char buffer[1025] = { 0 };
            int n;
            n = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);
            if (n == 0) {
                close(sock);
                FD_CLR(sock, &openSockets);
                clients.erase(sock);
            }
            else {
                parse_client_command(sock, buffer, openSockets);
            }
        }
    }
}

void server_commands(fd_set &openSockets, fd_set &readSockets) {
    for(auto const& server : servers) {
        int sock = server.second->sock;
        if (FD_ISSET(sock, &readSockets)) {
            std::string str;
            size_t idx = 0;
            char buffer[1025] = {0};
            int n;

            n = recv(sock, buffer, sizeof(buffer), MSG_DONTWAIT);

            if(n == 0){
                close(sock);
                FD_CLR(sock, &openSockets);
                servers.erase(sock);
            }
            else {
                str = buffer;

                if(str[0] != '\x01') {
                    std::cout << "Wrong format!" << std::endl;
                    return;
                }

                int split = 0;
                for(int i = 1; i < str.length(); i++) {
                    if(str[i] == '\x04') {
                        std::string command = str.substr(1, i-1);
                        parse_server_command(sock, openSockets, command.c_str());
                    }
                }
            }
        }
    }
}

/* Accept new connection to server */
void accept_client_connections(int sock, fd_set &openSockets, fd_set &readSockets) {
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t size;

    if (FD_ISSET(sock, &readSockets)) {
        client_fd = accept(sock,(struct sockaddr *)&client_addr, &size);

        clients[client_fd] = new Client(client_fd);

        FD_SET(client_fd, &openSockets);
        std::string msg = "You are now connected to the server owned by tsamvgroup88";
        std::cout << "Server: " + msg << std::endl;
        write(clients[client_fd]->sock, msg.c_str(), msg.size() + 1);
    }
}

/* Accept another server connecting to this server */
void accept_server_connections(int sock, fd_set &openSockets, fd_set &readSockets) {
    int serv_sock;
    struct sockaddr_in serv_addr;
    socklen_t size;
    char addr[INET_ADDRSTRLEN];
    std::string host;
    std::string port;
    if (FD_ISSET(sock, &readSockets)) {
        if(servers.size() < MAX_SERVER_CONNECTIONS) {
            // Server is not full so we can accept
            std::cout << "Server: Connection to another server established." << std::endl;
            serv_sock = accept(sock,(struct sockaddr *)&serv_addr, &size);
            FD_SET(serv_sock, &openSockets);

            // Add server to servers table
            port = std::to_string(serv_addr.sin_port);
            inet_ntop(AF_INET, &(serv_addr.sin_addr), addr, INET_ADDRSTRLEN);
            host = addr;
            std::cout << "port on new connection: " << port << std::endl;
            servers[serv_sock] = new Server(serv_sock, host, atoi(port.c_str()));
            //send and recv listservers command to get back the group id of the server we just connected to
            std::string list_servers_command = "\x01LISTSERVERS," + SERVER_ID + "\x04";
            write(serv_sock, list_servers_command.c_str(), list_servers_command.length());
        }
        else {
            // Decline connection since server is full
            std::cout << "Server: Connection refused! Connection limit reached." << std::endl;
            serv_sock = accept(sock,(struct sockaddr *)&serv_addr, &size);
            close(serv_sock);
        }
    }
}

void keepAlive(fd_set &openSockets, fd_set &readSockets) {
    //Send keepalive message to all servers
    //Check if any server has not sent us a keepalive message in 15-30 minutes and erase them.
    std::cout << "Sending KEEPALIVE to all connected servers!" << std::endl;
    for(auto const& server : servers) {
        int sock = server.second->sock;
        if(difftime(time(0), server.second->timeSinceLastKeepAlive) > 900) {
            close(sock);
            FD_CLR(sock, &openSockets);
            servers.erase(sock);
        }
        else {
            std::cout << "Sending keepAlive Message!" << std::endl;
            std::string keepAliveCommand = "\x01KEEPALIVE,0\x04";
            write(sock, keepAliveCommand.c_str(), keepAliveCommand.length());
        }
    }
    keepAliveCountdown = time(0);
}

int main(int argc, char* argv[]) {
    //int maxfds;
    int sock_client;
    int sock_server;
    int server_port;
    int client_port;
    fd_set openSockets;
    fd_set readSockets;

    if(argc != 2) {
        std::cout << "Usage: ./tsamvgroup88 <server_port>" << std::endl;
        exit(0);
    }

    //EXTERNAL_IP = get_external_ip();
    SERVER_PORT = atoi(argv[1]);

    /* Create listening socket for client port */
    sock_client = open_socket(CLIENT_PORT);

    /* Create listening socket for servers tcp port */
    sock_server = open_socket(SERVER_PORT);

    //If we have problems with FD_SETSIZE -> check this out and original server template code
    //maxfds = sock_client + sock_server;

    std::cout << "Server running on port: " << SERVER_PORT << std::endl;

    //Initialize the set of active sockets.
    FD_ZERO(&openSockets);
    FD_SET(sock_client, &openSockets);
    FD_SET(sock_server, &openSockets);
    

    bool finished = false;
    while(!finished) {
        readSockets = openSockets;

        if (select(FD_SETSIZE, &readSockets, NULL, NULL, &waitTime) < 0) {
            std::cout << "Select failed!" << std::endl;
            finished = true;
        }
        else {
            accept_client_connections(sock_client, openSockets, readSockets);
            accept_server_connections(sock_server, openSockets, readSockets);

            client_commands(openSockets, readSockets);
            server_commands(openSockets, readSockets);

            if(difftime(time(0), keepAliveCountdown) > 60) {
                keepAlive(openSockets, readSockets);
            }           
        }
    }
}
