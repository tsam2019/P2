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
const int CLIENT_PORT = 4088;
int SERVER_PORT;

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
    std::string port;

    Server(int _sock, std::string _host, std::string _port) {
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
std::map<int, Server*> servers;

// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.

/* Given a string split it on the delimiter given */
std::vector<std::string> stringSplit(std::string str, std::string delim) {
    std::vector<std::string> tokens;

    size_t pos = 0;
    std::string token;
    while ((pos = str.find(delim)) != std::string::npos) {
        token = str.substr(0, pos);
        tokens.push_back(token);
        str.erase(0, pos + delim.length());
    }
    tokens.push_back(str);
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
        if(listen(sock, MAX_SERVER_CONNECTIONS) < 0)
        {
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

     // If this client's socket is maxfds then the next lowest
     // one has to be determined. Socket fd's can be reused by the Kernel,
     // so there aren't any nice ways to do this.
     if(*maxfds == clientSocket) {
        for(auto const& p : clients) {
            *maxfds = std::max(*maxfds, p.second->sock);
        }
     }

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

// Process command from server on the server
void serverCommands(int serverSock, fd_set *openSockets, char *buffer) {
  std::vector<std::string> tokens = stringSplit(buffer, "'");

  if((tokens[0].compare("LISTSERVERS") == 0) && (tokens.size() == 2)) {
     //Send some shit to tokens[1] server with the list server command
     //add repsone to list of repsonses from this server
  }
  else if(tokens[0].compare("KEEPALIVE") == 0 && (tokens.size() == 2)) {
      //Fuckin make a function that sends messages periodically to all server tokens[1] number of times
  }
  else if(tokens[0].compare("GET_MSG") == 0 && (tokens.size() == 2)) {
      //Bruh, request messages from tokens[1] server
  }
  else if(tokens[0].compare("SEND_MSG") == 0) {
      //Tokens[0] = SEND MSG
      //Tokens[1] = FROM_GROUP_ID
      //Tokens[2] = TO_GROUP_ID
  }
  else if(tokens[0].compare("LEAVE") == 0) {
      
  }
  else if(tokens[0].compare("STATUSREQ") == 0) {
      
  }
  else {
      std::cout << "Unknown command from client:" << buffer << std::endl;
  }
}

void connect_to_server(std::string host, std::string port, fd_set &openSockets) {
    int sock;
    struct sockaddr_in serv_addr;
    int set = 1; // Toggle for setsockopt
    char addr[INET_ADDRSTRLEN];
    
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    serv_addr = get_sockaddr_in(host.c_str(), atoi(port.c_str()));

    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
        std::cout << "setsockopt failed" << std::endl;

    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0){
        std::cout << "Server: connection to host " + host + " on port " + port + " successful" << std::endl;
        // Add it to the active set
        FD_SET(sock, &openSockets);
        // Add server to server table
        servers[sock] = new Server(sock, host, port);
    }
}

bool parse_client_command(int clientSock, char* buffer, fd_set &openSockets) {
    std::vector<std::string> tokens = stringSplit(buffer, "'");

    if((tokens[0].compare("CONNECT") == 0) && (tokens.size() == 3)) {
        //tokens[1] is the host
        //tokens[2] is the port
        connect_to_server(tokens[1], tokens[2], openSockets);
    }
    else if(tokens[0].compare("GETMSG") == 0 && (tokens.size() == 2)) {
        //tokens[1] is the group id of the server we want a message from
    }
    else if(tokens[0].compare("SENDMSG") == 0 && (tokens.size() == 2)) {
        //tokens[1] is the group id of the recieving server
    }
    else if(tokens[0].compare("LISTSERVERS") == 0 && (tokens.size() == 1)) {
        //no other token
    }
    else {
        std::cout << "Unknown command from client:" << buffer << std::endl;
    }

    return true;
}

// Process command from client on the server
bool receive_from_client(int clientSock, fd_set &openSockets) {
    char buffer[1025] = { 0 };
    bool disconnect;
    int n;

    n = read(clientSock, buffer, sizeof(buffer));
    if (n <= 0) {
        return false;
    } 

    return parse_client_command(clientSock, buffer, openSockets);
}

/* Handle commands coming from connected clients */
void client_commands(fd_set &openSockets, fd_set &readSockets) {
    for(auto it = clients.cbegin(); it != clients.cend();) {
        int sock = it->second->sock;
        bool disconnect = false;
        if(FD_ISSET(sock, &readSockets)) {
            if(!receive_from_client(sock, openSockets)) {
                close(sock);
                FD_CLR(sock, &openSockets);
                it = clients.erase(it);
            }
        }
        else {
            ++it;
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
        write(clients[client_fd]->sock, msg.c_str(), msg.size() + 1);
    }
}

/* Accept another server connecting to this server */
void accept_server_connections(int sock, fd_set &activeSockets, fd_set &readSockets) {
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
            FD_SET(serv_sock, &activeSockets);

            // Add server to servers table
            port = std::to_string(serv_addr.sin_port);
            inet_ntop(AF_INET, &(serv_addr.sin_addr), addr, INET_ADDRSTRLEN);
            host = addr;
            servers[serv_sock] = new Server(serv_sock, host, port);
        }
        else {
            // Decline connection since server is full
            std::cout << "Server: Connection refused! Connection limit reached." << std::endl;
            serv_sock = accept(sock,(struct sockaddr *)&serv_addr, &size);
            close(serv_sock);
        }
    }
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

        if (select(FD_SETSIZE, &readSockets, NULL, NULL, NULL) < 0) {
            std::cout << "Select failed!" << std::endl;
            finished = true;
        }
        else {
            accept_client_connections(sock_client, openSockets, readSockets);
            accept_server_connections(sock_server, openSockets, readSockets);

            client_commands(openSockets, readSockets);
            //serverCommands(openSockets, readSockets);
        }
    }
}
