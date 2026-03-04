PROTO_DIR   := proto
BACKEND_DIR := path-tracer-backend
GEN_OUT     := .

export PATH := $(shell go env GOPATH)/bin:$(PATH)

PROTO_FILES := $(wildcard $(PROTO_DIR)/*.proto)

.PHONY: all proto build tidy fmt vet clean install-tools

FRONTEND_GEN := path-tracer-frontend/src/gen

install-tools:
	# Go proto plugins
	go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
	go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest
	go install connectrpc.com/connect/cmd/protoc-gen-connect-go@latest
	# TypeScript proto plugins
	npm install -g @bufbuild/protoc-gen-es @connectrpc/protoc-gen-connect-es

all: proto build

## Generate Go + TypeScript code from .proto files
proto: proto-go proto-ts

proto-go:
	protoc \
		--go_out=$(GEN_OUT) \
		--go_opt=paths=import \
		--go-grpc_out=$(GEN_OUT) \
		--go-grpc_opt=paths=import \
		--connect-go_out=$(GEN_OUT) \
		--connect-go_opt=paths=import \
		--proto_path=$(PROTO_DIR) \
		$(PROTO_FILES)

proto-ts:
	mkdir -p $(FRONTEND_GEN)
	protoc \
		--es_out=$(FRONTEND_GEN) \
		--es_opt=target=ts \
		--connect-es_out=$(FRONTEND_GEN) \
		--connect-es_opt=target=ts \
		--proto_path=$(PROTO_DIR) \
		$(PROTO_FILES)

tidy:
	cd $(BACKEND_DIR) && go mod tidy

fmt:
	cd $(BACKEND_DIR) && go fmt ./...

vet:
	cd $(BACKEND_DIR) && go vet ./...

build: tidy fmt vet
	cd $(BACKEND_DIR) && go build -o bin/server .

clean:
	rm -rf $(BACKEND_DIR)/gen
	rm -rf $(BACKEND_DIR)/bin
