# Tests Makefile

CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -g -I.. -I../collaboration -I../thirdparty
LDFLAGS = -lm

# POSIX flags
CFLAGS += -D_POSIX_C_SOURCE=200809L

# Source files for collaboration modules
COLLAB_SRCS = ../collaboration/collab_util.c \
              ../collaboration/collab_protocol.c \
              ../collaboration/collab_net.c \
              ../collaboration/collab_sync.c \
              ../collaboration/collab_server.c \
              ../collaboration/collab_client.c

# Field/gbuffer sources
FIELD_SRCS = ../field.c ../gbuffer.c

# All tests
TESTS = test_collab_util test_collab_protocol test_collab_net test_collab_sync test_integration

.PHONY: all clean run test

all: $(TESTS)

test_collab_util: test_collab_util.c ../collaboration/collab_util.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_collab_protocol: test_collab_protocol.c ../collaboration/collab_util.c ../collaboration/collab_protocol.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_collab_net: test_collab_net.c ../collaboration/collab_util.c ../collaboration/collab_protocol.c ../collaboration/collab_net.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_collab_sync: test_collab_sync.c $(COLLAB_SRCS) $(FIELD_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_integration: test_integration.c $(COLLAB_SRCS) $(FIELD_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: all
	@echo ""
	@echo "=========================================="
	@echo "Running collab_util tests..."
	@echo "=========================================="
	./test_collab_util
	@echo ""
	@echo "=========================================="
	@echo "Running collab_protocol tests..."
	@echo "=========================================="
	./test_collab_protocol
	@echo ""
	@echo "=========================================="
	@echo "Running collab_net tests..."
	@echo "=========================================="
	./test_collab_net
	@echo ""
	@echo "=========================================="
	@echo "Running collab_sync tests..."
	@echo "=========================================="
	./test_collab_sync
	@echo ""
	@echo "=========================================="
	@echo "Running integration tests..."
	@echo "=========================================="
	./test_integration

test: run

clean:
	rm -f $(TESTS)
