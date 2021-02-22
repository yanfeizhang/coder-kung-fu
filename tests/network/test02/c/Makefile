CLIENT_BINARY_NAME := "test-client"
SERVER_BINARY_NAME := "test-server"

.PHONY: ping
ping:
	sh tool.sh ping

.PHONY: ifup
ifup:
	sh tool.sh ifup

.PHONY: ifdown
ifdown:
	sh tool.sh ifdown

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

.PHONY: clean
clean:
	rm -f ${CLIENT_BINARY_NAME}
	rm -f $(SERVER_BINARY_NAME)
