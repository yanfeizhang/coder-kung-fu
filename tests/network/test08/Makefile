SERVER_BINARY_NAME := "test-server"
CLIENT_BINARY_NAME := "test-client"

.PHONY: build-server
build-server:
	gcc server.c -o $(SERVER_BINARY_NAME)

.PHONY: run-server
run-server:
	sh tool.sh runsrv ./$(SERVER_BINARY_NAME)

.PHONY: stop-server
stop-server:
	sh tool.sh stopsrv ./$(SERVER_BINARY_NAME)

.PHONY: build-client
build-client:
	gcc client.c -o $(CLIENT_BINARY_NAME)

.PHONY: run-client
run-client:
	./$(CLIENT_BINARY_NAME)	0.0.0.0 6000	