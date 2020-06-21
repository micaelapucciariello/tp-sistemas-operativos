#include "team.h"

int main(int argc, char *argv[]) {
	if (team_load() < 0)
		return EXIT_FAILURE;
	team_init();
	team_exit();

	return EXIT_SUCCESS;
}

int team_load() {
	int response = team_config_load();
	if (response < 0)
		return response;

	response = team_logger_create(team_config->log_file);
	if (response < 0) {
		team_config_free();
		return response;
	}
	team_print_config();

	return 0;
}

void team_init() {

	pthread_mutex_init(&planner_mutex, NULL);
	pthread_attr_t attrs;
	pthread_attr_init(&attrs);
	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
	pthread_t tid;
	pthread_t tid2;
	pthread_t tid3;
	pthread_t planificator;
	team_planner_init();
	send_get_message();
	sem_wait(&sem_entrenadores);

	for (int i = 0; i < list_size(new_queue); i++) {
		t_entrenador_pokemon* entrenador;
		entrenador = list_get(new_queue, i);
		pthread_t thread_entrenadores;
		pthread_create(&thread_entrenadores, NULL, (void*) move_trainers, entrenador);	
		pthread_detach(&thread_entrenadores);
	}

	sem_t sem_message_on_queue;
	sem_init(&sem_message_on_queue, 0, 0);

	pthread_create(&planificator, NULL, (void*) team_planner_run_planification, NULL);
	pthread_detach(&planificator);

	team_logger_info("Creando un hilo para subscribirse a la cola APPEARED del broker %d");
	t_cola cola_appeared = APPEARED_QUEUE;
	pthread_create(&tid, NULL, (void*) team_retry_connect, (void*) &cola_appeared);
	pthread_detach(tid);

	team_logger_info("Creando un hilo para subscribirse a la cola LOCALIZED del broker %d");

	t_cola cola_localized = LOCALIZED_QUEUE;
	pthread_create(&tid2, NULL, (void*) team_retry_connect, (void*) &cola_localized);
	pthread_detach(tid2);

	team_logger_info("Creando un hilo para subscribirse a la cola CAUGHT del broker %d");

	t_cola cola_caught = CAUGHT_QUEUE;
	pthread_create(&tid3, NULL, (void*) team_retry_connect, (void*) &cola_caught);
	pthread_detach(tid3);

	pthread_create(&tid4, NULL, (void*) send_message_test, NULL);
	pthread_detach(tid4);

	team_logger_info("Creando un hilo para poner al Team en modo Servidor");
	team_server_init();
	usleep(500000);
	for (;;)
		;

}

void remove_pokemon_from_catch (t_catch_pokemon* catch_message) {
	for (int i = 0; i < list_size(pokemon_to_catch); i++) {
		t_pokemon_received* pokemon_con_posiciones = list_get(pokemon_to_catch, i);
		if (string_equals_ignore_case(pokemon_con_posiciones->name, catch_message->nombre_pokemon)) {
			t_list* posiciones_pokemon = pokemon_con_posiciones->pos;
			for (int j = 0; j < list_size(posiciones_pokemon); j++) {
				t_position* position = list_get(posiciones_pokemon, j);
				if (position->pos_x == catch_message->pos_x && position->pos_y == catch_message->pos_y) {
					list_remove(pokemon_to_catch, i);
				}
			}
		}
	}
}

void send_message_catch(t_catch_pokemon* catch_send, t_entrenador_pokemon* entrenador) {
	t_protocol catch_protocol;

	int i = send_message(catch_send, catch_protocol, NULL);

	if (i == 0) {
		team_logger_info("Catch sent!");
		team_planner_block_current_trainner(catch_send->pokemon_name, 1); 
		//TODO
		//cuando recibo un caught vuelvo a poner en 0 al status
		list_add(message_catch_sended, catch_send);
		list_add(entrenador->list_id_catch, catch_send->id_correlacional);

	} else {
		remove_pokemon_from_catch (catch_send);
		//TODO
		//chequear si el estrenador está completo. si está terminado. bloquear al pokemon
	}

	usleep(500000);
	// To broker
}

void send_get_message() {
	t_protocol get_protocol;
	t_get_pokemon* get_send = malloc(sizeof(t_get_pokemon));

	for (int i = 0; i < list_size(keys_list); i++) {
		char* nombre = list_get(keys_list, i);
		get_send->id_correlacional = 0;
		get_send->nombre_pokemon = string_duplicate(nombre);
		get_send->tamanio_nombre = strlen(get_send->nombre_pokemon) + 1;
		get_protocol = GET_POKEMON;
		
		int i = send_message(get_send, get_protocol, get_id_corr);
		if (i > 0) {
			team_logger_info("Se recibió un id correlacional en respuesta a un get: %d", id_corr);
		}
		usleep(500000);
	}
}


int send_message(void* paquete, t_protocol protocolo, t_list* queue) {
	int broker_fd_send = socket_connect_to_server(team_config->ip_broker, team_config->puerto_broker);

	if (broker_fd_send < 0) {
		team_logger_warn("No se pudo conectar con BROKER");
		socket_close_conection(broker_fd_send);
		return -1;
	} else {
		team_logger_info("Conexion con BROKER establecida correctamente!");
		utils_serialize_and_send(broker_fd_send, protocolo, paquete);

		uint32_t id_corr;
		int recibido = recv(broker_fd_send, id_corr, sizeof(uint32_t), MSG_WAITALL);
		if (recibido > 0 && queue != NULL) {
			list_add(queue, id_corr);
		}
		if (protocolo == CATCH_POKEMON) {
			t_catch_pokemon *catch_send = (t_catch_pokemon*) paquete;
			catch_send->id_correlacional = id_corr;
		}
	}
	return 0;
}

void move_trainers(t_entrenador_pokemon* entrenador) {
	sem_wait(&entrenador->sem_trainer);
	int aux_x;
	int aux_y;
	int steps;

	steps = fabs(aux_x + aux_y);
	sleep(steps*(team_config->retardo_ciclo_cpu)); 
	//pokemon temporal el pokemon que planifico en el momento. lo debe modificar el algoritmo de cercanía al terminar su ejecucion

	aux_x = entrenador->position->pos_x - pokemon_temporal->position->pos_x;
	aux_y = entrenador->position->pos_y - pokemon_temporal->position->pos_y;
	//buscar mecanismo para contar rafagas de cpu

	team_logger_info((
			"Un trainer se movio de:\n\n"
			"x\t\t%d\n"
			"y:\t\t%d\n"
			"a:\n\n"
			"x\t\t%d\n"
			"y:\t\t%d\n",
			entrenador->position->pos_x,
			entrenador->position->pos_y,
			pokemon_temporal->position->pos_x,
			pokemon_temporal->position->pos_y
		);

	entrenador->position->pos_x = pokemon_temporal->position->pos_x;
	entrenador->position = pokemon_temporal->position->pos_y;



	//TODO
	//muevo, cuento tiempo, logueo movimiento

	t_catch_pokemon* catch_send = malloc(sizeof(t_catch_pokemon));
	catch_send->id_correlacional = 0;
	catch_send->nombre_pokemon = pokemon_temporal->name;
	catch_send-> pos_x = pokemon_temporal->position->pos_x;
	catch_send->pos_y = pokemon_temporal->position->pos_y;
	catch_send->tamanio_nombre = strlen(catch_send->nombre_pokemon);
	catch_protocol = CATCH_POKEMON;
	send_message_catch(catch_send, entrenador); 

	sem_post(&sem_planification);
}

void subscribe_to(void *arg) {

	t_cola cola = *((int *) arg);
	team_logger_info("tipo Cola: %d ", cola);
	switch (cola) {
	case NEW_QUEUE: {
		team_logger_info("Cola NEW ");
		break;
	}
	case CATCH_QUEUE: {
		team_logger_info("Cola CATCH ");
		break;
	}
	case CAUGHT_QUEUE: {
		team_logger_info("Cola CAUGHT ");
		break;
	}
	case GET_QUEUE: {
		team_logger_info("Cola GET ");
		break;
	}
	case LOCALIZED_QUEUE: {
		team_logger_info("Cola LOCALIZED ");
		break;
	}
	case APPEARED_QUEUE: {
		team_logger_info("Cola APPEARED ");
		break;
	}
	}

	int new_broker_fd = socket_connect_to_server(team_config->ip_broker, team_config->puerto_broker);

	if (new_broker_fd < 0) {
		team_logger_warn("No se pudo conectar con BROKER");
		socket_close_conection(new_broker_fd);
	} else {
		team_logger_info("Conexion con BROKER establecida correctamente!");
		t_subscribe* sub_snd = malloc(sizeof(t_subscribe));

		t_protocol subscribe_protocol = SUBSCRIBE;
		sub_snd->ip = string_duplicate(team_config->ip_team);
		sub_snd->puerto = team_config->puerto_team;
		sub_snd->proceso = TEAM;
		sub_snd->cola = cola;
		utils_serialize_and_send(new_broker_fd, subscribe_protocol, sub_snd);

		receive_msg(new_broker_fd, 0);
		is_connected = true;
	}
}

void team_retry_connect(void* arg) {
	void* arg2 = arg;
	while (true) {
		is_connected = false;
		subscribe_to(arg2);
		utils_delay(team_config->tiempo_reconexion);
	}
}

void *receive_msg(int fd, int send_to) {
	int protocol;
	int is_server = send_to;

	while (true) {
		int received_bytes = recv(fd, &protocol, sizeof(int), 0);
		if (received_bytes <= 0) {
			team_logger_error("Se perdio la conexion");
			return NULL;
		}

		switch (protocol) {
			case CAUGHT_POKEMON: {
				team_logger_info("Caught received");
				t_caught_pokemon *caught_rcv = utils_receive_and_deserialize(fd,
						protocol);
				team_logger_info("ID correlacional: %d",
						caught_rcv->id_correlacional);
				team_logger_info("Resultado (0/1): %d", caught_rcv->result);
				usleep(50000);
				break;

				team_planner_change_block_status(0, caught_rcv->id_correlacional)//no tiene mensajes pendientes 
				//TODO
			}

			case LOCALIZED_POKEMON: {
				team_logger_info("Localized received");
				t_localized_pokemon *loc_rcv = utils_receive_and_deserialize(fd, protocol);
				team_logger_info("ID correlacional: %d", loc_rcv->id_correlacional);
				team_logger_info("Nombre Pokemon: %s", loc_rcv->nombre_pokemon);
				team_logger_info("Largo nombre: %d", loc_rcv->tamanio_nombre);
				team_logger_info("Cant Elementos en lista: %d", loc_rcv->cant_elem);
				for (int el = 0; el < loc_rcv->cant_elem; el++) {
					t_position* pos = malloc(sizeof(t_position));
					pos = list_get(loc_rcv->posiciones, el);
					team_logger_info("Position is (%d, %d)", pos->pos_x, pos->pos_y);
				}
				usleep(500000);

				bool _es_el_mismo(uint32_t id) {
					return loc_rcv->id_correlacional == id;
				}

				if (list_any_satisfy(get_id_corr, (void*) _es_el_mismo)
						&& pokemon_required(loc_rcv->nombre_pokemon)) {
					t_pokemon_received* pokemon = malloc(sizeof(t_pokemon_received));
					pokemon->name = malloc(sizeof(loc_rcv->tamanio_nombre));
					pokemon->name = loc_rcv->nombre_pokemon;
					pokemon->pos = list_create();
					pokemon->pos = loc_rcv->posiciones;
					list_add(pokemon_to_catch, pokemon);
					sem_post(&sem_message_on_queue);
				}
				break;
			}

			case APPEARED_POKEMON: {
				team_logger_info("Appeared received");
				t_appeared_pokemon *appeared_rcv = utils_receive_and_deserialize(fd, protocol);
				team_logger_info("ID correlacional: %d", appeared_rcv->id_correlacional);
				team_logger_info("Cantidad: %d", appeared_rcv->cantidad);
				team_logger_info("Nombre Pokemon: %s", appeared_rcv->nombre_pokemon);
				team_logger_info("Largo nombre: %d", appeared_rcv->tamanio_nombre);
				team_logger_info("Posicion X: %d", appeared_rcv->pos_x);
				team_logger_info("Posicion Y: %d", appeared_rcv->pos_y);
				usleep(50000);

				if (is_server == 0) {
					pthread_t tid;
					pthread_create(&tid, NULL, (void*) send_ack, (void*) &appeared_rcv->id_correlacional);
					pthread_detach(tid);
				}

				if (pokemon_required(appeared_rcv->nombre_pokemon)) {
					t_position* posicion = malloc(sizeof(t_position));
					posicion->pos_x = appeared_rcv->pos_x;
					posicion->pos_y = appeared_rcv->pos_y;
					t_pokemon_received* pokemon = malloc(sizeof(t_pokemon_received));
					pokemon->name = malloc(sizeof(appeared_rcv->tamanio_nombre));
					pokemon->name = appeared_rcv->nombre_pokemon;
					pokemon->pos = list_create();
					list_add(pokemon->pos, posicion);
					list_add(pokemon_to_catch, pokemon);
					sem_post(&sem_message_on_queue);
				}

				break;
			}

			default:
				break;
		}
	}
	return NULL;
}

bool pokemon_required(char* pokemon_name) {

	bool _es_el_mismo(char* name) {
		return  string_equals_ignore_case(pokemon_name,name);
	}

	char* _get_name(t_pokemon_received* pokemon) {
		return pokemon->name;
	}

	t_list* pokemon_to_catch_name = list_map(pokemon_to_catch, (void*) _get_name);

	if (list_any_satisfy(pokemon_to_catch_name, (void*) _es_el_mismo)) {
		return false;
	}
	return true;
}

void team_server_init() {

	team_socket = socket_create_listener(team_config->ip_team, team_config->puerto_team);
	if (team_socket < 0) {
		team_logger_error("Error al crear server");
		return;
	}

	team_logger_info("Server creado correctamente!! Esperando conexiones...");

	struct sockaddr_in client_info;
	socklen_t addrlen = sizeof client_info;

	pthread_attr_t attrs;
	pthread_attr_init(&attrs);
	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);

	for (;;) {
		int accepted_fd;
		pthread_t tid;

		if ((accepted_fd = accept(team_socket, (struct sockaddr *) &client_info, &addrlen)) != -1) {

			t_handle_connection* connection_handler = malloc( sizeof(t_handle_connection));
			connection_handler->fd = accepted_fd;
			connection_handler->bool_val = 1;

			pthread_create(&tid, NULL, (void*) handle_connection, (void*) connection_handler);
			pthread_detach(tid);
			team_logger_info("Creando un hilo para atender una conexión en el socket %d", accepted_fd);
		} else {
			team_logger_error("Error al conectar con un cliente");
		}
	}
}

void *handle_connection(void *arg) {
	t_handle_connection* connect_handler = (t_handle_connection *) arg;
	int client_fd = connect_handler->fd;
	receive_msg(client_fd, connect_handler->bool_val);
	return NULL;
}

void send_ack(void* arg) {
	int val = *((int*) arg);
	t_ack* ack_snd = malloc(sizeof(t_ack));
	t_protocol ack_protocol = ACK;
	ack_snd->id = val;


	int client_fd = socket_connect_to_server(team_config->ip_broker, team_config->puerto_broker);
	if (client_fd > 0) {
		utils_serialize_and_send(client_fd, ack_protocol, ack_snd);
		team_logger_info("ACK SENT TO BROKER");
	}
	team_logger_info("CONNECTION WITH BROKER WILL BE CLOSED");
	socket_close_conection(client_fd);
}

void team_exit() {
	socket_close_conection(team_socket);
//socket_close_conection(broker_fd);
	pthread_mutex_destroy(&planner_mutex);
	team_planner_destroy();
	team_config_free();
	team_logger_destroy();
}
