all:
	g++ -g server.cpp hashtable.cpp -o server.exe -lws2_32
	g++ -g client.cpp hashtable.cpp -o client.exe -lws2_32
	g++ -g avl.cpp test_avl.cpp -o test_avl.exe

clean:
	del server.exe client.exe