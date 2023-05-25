#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/common.h"
#include "uintr.h"

#define SERVER_TOKEN 0
#define CLIENT_TOKEN 1

volatile unsigned long uintr_received[2];
volatile int uintrfd_client;
volatile int uintrfd_server;
int client_ready = 0, server_ready = 0;
int uipi_index[2];

uint64_t uintr_handler(struct __uintr_frame* ui_frame, uint64_t irqs) {
	int cnt = -1;
	if (irqs == 0) {
		printf("Error: No irqs!\n");
		return 0;
	}
	while (irqs > 0) {
		++cnt;
		irqs >>= 1;
	}
	uintr_received[cnt] = 1;
	return 0;
}


int setup_handler_with_vector(int vector) {
	int fd;

	if (__register_receiver(uintr_handler))
		throw("Interrupt handler register error\n");

	// Create a new uintrfd object and get the corresponding
	// file descriptor.
	fd = uintr_create_fd(vector);

	if (fd < 0) throw("Interrupt vector registration error\n");

	return fd;
}

void uintrfd_wait(unsigned int token) {
	// Keep spinning until the interrupt is received
	while (!uintr_received[token])
		;

	uintr_received[token] = 0;
}

void uintrfd_notify(unsigned int token) {
	uipi_send(uipi_index[token]);
}

void setup_client(void) {
	uintrfd_client = setup_handler_with_vector(CLIENT_TOKEN);
	printf("%s %d uintrfd_client = %d\n", __FUNCTION__, __LINE__, uintrfd_client);

	// Wait for client to setup its FD.
	while (!uintrfd_server)
		;

	uipi_index[SERVER_TOKEN] = uintr_register_sender(uintrfd_server);
}

void setup_server(void) {
	uintrfd_server = setup_handler_with_vector(SERVER_TOKEN);
	printf("%s %d uintrfd_server = %d\n", __FUNCTION__, __LINE__, uintrfd_server);

	// Wait for client to setup its FD.
	while (!uintrfd_client)
		;

	uipi_index[CLIENT_TOKEN] = uintr_register_sender(uintrfd_client);
}

void* client_communicate(void* arg) {
	struct Arguments* args = (struct Arguments*)arg;
	int loop;

	setup_client();

	for (loop = args->count; loop > 0; --loop) {
		uintrfd_wait(CLIENT_TOKEN);
		uintrfd_notify(SERVER_TOKEN);
	}

	return NULL;
}

void server_communicate(struct Arguments* args) {
	struct Benchmarks bench;
	int message;

	setup_server();

	setup_benchmarks(&bench);

	for (message = 0; message < args->count; ++message) {
		uintrfd_notify(CLIENT_TOKEN);
		uintrfd_wait(SERVER_TOKEN);
	}

	// The message size is always one (it's just a signal)
	args->size = 1;
	evaluate(&bench, args);
}

void communicate(struct Arguments* args) {
	pthread_t pt;

	// Create another thread
	if (pthread_create(&pt, NULL, &client_communicate, args)) {
		throw("Error creating sender thread");
	}

	server_communicate(args);
}

int main(int argc, char* argv[]) {
	struct Arguments args;

	parse_arguments(&args, argc, argv);

	communicate(&args);

	return EXIT_SUCCESS;
}