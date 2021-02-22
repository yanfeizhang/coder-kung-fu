CLIENT_BINARY_NAME := "test-client"
SERVER_BINARY_NAME := "test-server"

.PHONY: build-cli
build-cli:
	gcc client.c -o $(CLIENT_BINARY_NAME)

.PHONY: run-cli
run-cli: build-cli
	sh tool.sh runcli ./$(CLIENT_BINARY_NAME)

.PHONY: stop-cli
stop-cli:
	sh tool.sh stopcli ./$(CLIENT_BINARY_NAME)

.PHONY: build-srv
build-srv:
	gcc server.c -o $(SERVER_BINARY_NAME)

.PHONY: run-srv
run-srv: build-srv
	sh tool.sh runsrv ./$(SERVER_BINARY_NAME)

.PHONY: stop-srv
stop-srv: 
	sh tool.sh stopsrv ./$(SERVER_BINARY_NAME)

.PHONY: clean
clean:
	rm -f ${CLIENT_BINARY_NAME}
	rm -f $(SERVER_BINARY_NAME)
