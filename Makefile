all:
	@echo "Make all"
	g++ -std=c++11 client.cpp -lpthread -o client
	g++ -std=c++11 server.cpp -o server

run:
	@echo "Running All"
	sudo ./server 4069
	sudo ./client 127.0.0.1 4088
clean: 
	@echo "Cleaning up..."
	rm client
	rm server