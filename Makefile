all:
	g++ -g server.cpp -o server.exe -lws2_32
	g++ -g client.cpp -o client.exe -lws2_32

clean:
	del server.exe client.exe