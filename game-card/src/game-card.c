#include "game-card.h"

int main(int argc, char *argv[]) {
	if (game_card_load() < 0)
		return EXIT_FAILURE;
	game_card_init();
	game_card_exit();

	return EXIT_SUCCESS;
}

int game_card_load() {
	int response = game_card_logger_create();
	if (response < 0)
		return response;

	response = game_card_config_load();
	if (response < 0) {
		game_card_logger_destroy();
		return response;
	}
	return 0;
}

void game_card_init() {
	game_card_logger_info("Inicando GAMECARD..");
	gcfsCreateStructs();

	pthread_attr_t attrs;
	pthread_attr_init(&attrs);
	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
	pthread_t tid;
	pthread_t tid2;
	pthread_t tid3;
	t_cola new_queue;
	t_cola catch_queue;
	t_cola get_queue;

	game_card_logger_info( "Creando un hilo para subscribirse a la cola NEW del broker %d");
	new_queue = NEW_QUEUE;
	pthread_create(&tid, NULL, (void*) game_card_retry_connect, (void*) &new_queue);
	pthread_detach(tid);
	usleep(500000);

	game_card_logger_info("Creando un hilo para subscribirse a la cola CATCH del broker %d");
	catch_queue = CATCH_QUEUE;
	pthread_create(&tid2, NULL, (void*) game_card_retry_connect, (void*) &catch_queue);
	pthread_detach(tid2);
	usleep(500000);

	game_card_logger_info("Creando un hilo para subscribirse a la cola GET del broker %d");
	get_queue = GET_QUEUE;
	pthread_create(&tid3, NULL, (void*) game_card_retry_connect, (void *) &get_queue);
	pthread_detach(tid3);
	usleep(500000);

	game_card_logger_info("Creando un hilo para poner al GAMECARD en modo Servidor");
	game_card_init_as_server();
	usleep(500000);
	for (;;)
		;
}

void game_card_retry_connect(void* arg) {
	void* arg2 = arg;
	while (true) {
		is_connected = false;
		subscribe_to(arg2);
		utils_delay(game_card_config->tiempo_de_reintento_conexion);
	}
}

void subscribe_to(void *arg) {

	t_cola cola = *((int *) arg);
	int new_broker_fd = socket_connect_to_server(game_card_config->ip_broker,
			game_card_config->puerto_broker);

	if (new_broker_fd < 0) {
		game_card_logger_warn("No se pudo conectar la cola %d con BROKER",
				cola);
		socket_close_conection(new_broker_fd);
	} else {
		game_card_logger_info(
				"Conexion de la cola %d con BROKER establecida correctamente!",
				cola);
		t_subscribe* sub_snd = malloc(sizeof(t_subscribe));
		t_protocol subscribe_protocol = SUBSCRIBE;
		sub_snd->ip = string_duplicate(game_card_config->ip_game_card);
		sub_snd->puerto = game_card_config->puerto_game_card;
		sub_snd->proceso = GAME_CARD;
		sub_snd->cola = cola;
		utils_serialize_and_send(new_broker_fd, subscribe_protocol, sub_snd);
		recv_game_card(new_broker_fd, 0);
		is_connected = true;
	}
}

void game_card_init_as_server() {
	int game_card_socket = socket_create_listener(
			game_card_config->ip_game_card, game_card_config->puerto_game_card);
	if (game_card_socket < 0) {
		game_card_logger_error("Error al levantar GAMECARD server");
	}
	game_card_logger_info(
			"Server creado correctamente!! Esperando conexion del GAMEBOY");
	struct sockaddr_in client_info;
	socklen_t addrlen = sizeof client_info;
	pthread_attr_t attrs;
	pthread_attr_init(&attrs);
	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
	int accepted_fd;
	for (;;) {
		pthread_t tid;
		if ((accepted_fd = accept(game_card_socket,
				(struct sockaddr *) &client_info, &addrlen)) != -1) {
			t_handle_connection* connection_handler = malloc(
					sizeof(t_handle_connection));
			connection_handler->fd = accepted_fd;
			connection_handler->bool_val = 1;
			pthread_create(&tid, NULL, (void*) handle_connection,
					(void*) connection_handler);
			pthread_detach(tid);
			game_card_logger_info(
					"Creando un hilo para atender una conexión en el socket %d",
					accepted_fd);
			usleep(500000);
		} else {
			game_card_logger_error("Error al conectar con un cliente");
		}
	}
}

static void *handle_connection(void *arg) {
	t_handle_connection* connect_handler = (t_handle_connection *) arg;
	int client_fd = connect_handler->fd;
	recv_game_card(client_fd, connect_handler->bool_val);
	return NULL;
}
void *recv_game_card(int fd, int respond_to) {
	int received_bytes;
	int protocol;
	int client_fd = fd;

	// 1 = Receives from GB; 0 = Receives from Broker
	int is_server = respond_to;

	while (true) {
		received_bytes = recv(client_fd, &protocol, sizeof(int), 0);

		if (received_bytes <= 0) {
			game_card_logger_error("Error al recibir mensaje");
			return NULL;
		}
		switch (protocol) {

		// From Broker or GB
		case NEW_POKEMON: {
			game_card_logger_info("NEW received");
			t_new_pokemon *new_receive = utils_receive_and_deserialize(client_fd, protocol);
			game_card_logger_info("Operacion NEW_POKEMON %s, Coordenada: (%d, %d, %d)", new_receive->nombre_pokemon, new_receive->pos_x, new_receive->pos_y, new_receive->cantidad);
			game_card_logger_info("ID Correlacional: %d", new_receive->id_correlacional);
			usleep(100000);


			pthread_attr_t attrs;
			pthread_attr_init(&attrs);
			pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);

			if (is_server == 0) {
				t_protocol ack_protocol = ACK;
				game_card_logger_info("ACK SENT TO BROKER");

				t_ack* ack_send = malloc(sizeof(t_ack));
				ack_send->id_corr_msg = new_receive->id_correlacional;
				ack_send->queue = NEW_QUEUE;
				ack_send->sender_name = "GAMECARD";
				ack_send->ip = game_card_config->ip_game_card;
				ack_send->port = game_card_config->puerto_game_card;

				utils_serialize_and_send(client_fd, ack_protocol, ack_send);
			}

			pthread_t tid1;
			pthread_create(&tid1, NULL, (void*) process_new_and_send_appeared,
					(void*) new_receive);
			pthread_detach(tid1);

			break;
		}

			// From broker or GB
		case GET_POKEMON: {
			game_card_logger_info("GET received");
			t_get_pokemon *get_rcv = utils_receive_and_deserialize(client_fd, protocol);
			game_card_logger_info("Operacion GET_POKEMON %s", get_rcv->nombre_pokemon);
			game_card_logger_info("ID correlacional: %d", get_rcv->id_correlacional);
			usleep(50000);

			// To broker
			pthread_attr_t attrs;
			pthread_attr_init(&attrs);
			pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);

			if (is_server == 0) {
				t_protocol ack_protocol = ACK;
				game_card_logger_info("ACK SENT TO BROKER");

				t_ack* ack_send = malloc(sizeof(t_ack));
				ack_send->id_corr_msg = get_rcv->id_correlacional;
				ack_send->queue = GET_QUEUE;
				ack_send->sender_name = "GAMECARD";
				ack_send->ip = game_card_config->ip_game_card;
				ack_send->port = game_card_config->puerto_game_card;

				utils_serialize_and_send(client_fd, ack_protocol, ack_send);
			}

			pthread_t tid3;
			pthread_create(&tid3, NULL, (void*) process_get_and_send_localized,
					(void*) get_rcv);
			pthread_detach(tid3);

			break;
		}

			// From broker or GB
		case CATCH_POKEMON: {
			game_card_logger_info("CATCH received");
			t_catch_pokemon *catch_rcv = utils_receive_and_deserialize(client_fd, protocol);
			game_card_logger_info("Operacion CATCH_POKEMON %s, Coordenada: (%d, %d)", catch_rcv->nombre_pokemon, catch_rcv->pos_x, catch_rcv->pos_y);
			game_card_logger_info("ID correlacional: %d", catch_rcv->id_correlacional);
			usleep(50000);


			// To Broker
			pthread_attr_t attrs;
			pthread_attr_init(&attrs);
			pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);

			if (is_server == 0) {
				t_protocol ack_protocol = ACK;
				game_card_logger_info("ACK SENT TO BROKER");

				t_ack* ack_send = malloc(sizeof(t_ack));
				ack_send->id_corr_msg = catch_rcv->id_correlacional;
				ack_send->queue = CATCH_QUEUE;
				ack_send->sender_name = "GAMECARD";
				ack_send->ip = game_card_config->ip_game_card;
				ack_send->port = game_card_config->puerto_game_card;

				utils_serialize_and_send(client_fd, ack_protocol, ack_send);
			}

			pthread_t tid5;

			pthread_create(&tid5, NULL, (void*) process_catch_and_send_caught,
					(void*) catch_rcv);
			pthread_detach(tid5);
			break;
		}

		default:
			break;
		}
	}
}

void process_new_and_send_appeared(void* arg) {
	t_new_pokemon* new_receive = (t_new_pokemon*) arg;
	createNewPokemon(new_receive);

	// Process New and send Appeared to broker
	t_appeared_pokemon* appeared_snd = malloc(sizeof(t_appeared_pokemon));
	t_protocol appeared_protocol = APPEARED_POKEMON;
	appeared_snd->nombre_pokemon = new_receive->nombre_pokemon;
	appeared_snd->tamanio_nombre = new_receive->tamanio_nombre;
	appeared_snd->id_correlacional = new_receive->id_correlacional;
	appeared_snd->pos_x = new_receive->pos_x;
	appeared_snd->pos_y = new_receive->pos_y;
	int client_fd = socket_connect_to_server(game_card_config->ip_broker,
			game_card_config->puerto_broker);
	if (client_fd > 0) {
		utils_serialize_and_send(client_fd, appeared_protocol, appeared_snd);
		game_card_logger_info("APPEARED sent to BROKER");
	}
	usleep(500000);
	game_card_logger_info("CLOSING CONNECTION WITH BROKER");
	socket_close_conection(client_fd);
}

void process_get_and_send_localized(void* arg) {
	t_get_pokemon* get_rcv = (t_get_pokemon*) arg;
	t_list* response = getAPokemon(get_rcv);

	t_localized_pokemon* loc_snd = malloc(sizeof(t_localized_pokemon));
	loc_snd->id_correlacional = get_rcv->id_correlacional;
	loc_snd->nombre_pokemon = get_rcv->nombre_pokemon;
	loc_snd->tamanio_nombre = strlen(loc_snd->nombre_pokemon) + 1;

	t_list* positions_snd = list_create();

	for (int i=0; i< list_size(response); ++i) {
		t_position_aux* pos_aux = list_get(response, i);
		for (int j=0; j < pos_aux->cant; ++j) {
			t_position* pos = malloc(sizeof(t_position));
			pos->pos_x = pos_aux->x;
			pos->pos_y = pos_aux->y;

			list_add(positions_snd, pos);
		}
	}

	loc_snd->cant_elem = list_size(positions_snd);
	loc_snd->posiciones = positions_snd;

	t_protocol localized_protocol = LOCALIZED_POKEMON;

	int client_fd = socket_connect_to_server(game_card_config->ip_broker,
			game_card_config->puerto_broker);
	if (client_fd > 0) {
		utils_serialize_and_send(client_fd, localized_protocol, loc_snd);
		game_card_logger_info("LOCALIZED sent to BROKER");
	}
	usleep(50000);
	socket_close_conection(client_fd);
}

void process_catch_and_send_caught(void* arg) {
	t_catch_pokemon* catch_rcv = (t_catch_pokemon*) arg;
	int res = catchAPokemon(catch_rcv);

	t_caught_pokemon* caught_snd = malloc(sizeof(t_catch_pokemon));
	caught_snd->id_correlacional = catch_rcv->id_correlacional;
	caught_snd->result = res;
	game_card_logger_info("CAUGHT RESPONSE sent to BROKER %d", caught_snd->result);
	// Process Catch and send Caught to broker
	t_protocol caught_protocol = CAUGHT_POKEMON;

	int client_fd = socket_connect_to_server(game_card_config->ip_broker,
			game_card_config->puerto_broker);
	if (client_fd > 0) {
		utils_serialize_and_send(client_fd, caught_protocol, caught_snd);
		game_card_logger_info("CAUGHT sent to BROKER");
	}
	usleep(500000);
	socket_close_conection(client_fd);
}

void game_card_exit() {
	socket_close_conection(game_card_fd);
	//gcfsFreeBitmaps();
	game_card_config_free();
	game_card_logger_destroy();

	free(struct_paths[METADATA]);
	free(struct_paths[FILES]);
	free(struct_paths[BLOCKS]);
	free(struct_paths[TALL_GRASS]);
}
