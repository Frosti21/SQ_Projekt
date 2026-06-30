/*
 * mqtt_collector.c
 *
 * MQTT Datensammler für Raspberry Pi 5
 * Empfängt Daten von:
 *   - Shelly Plug S (JSON Format)
 *   - ESP32 (Rohe Werte)
 *
 * Speichert Daten in JSON-Dateien pro Gerät.
 * PC kann Daten via MQTT-Request abrufen.
 *
 * Abhängigkeiten:
 *   - libmosquitto   (apt install libmosquitto-dev)
 *   - cJSON          (apt install libcjson-dev)
 *
 * Kompilieren:
 *   gcc -o mqtt_collector mqtt_collector.c -lmosquitto -lcjson -lpthread
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <time.h>
 #include <pthread.h>
 #include <signal.h>
 #include <unistd.h>
 #include <sys/stat.h>
 #include <errno.h>
 #include <mosquitto.h>
 #include <cjson/cJSON.h>
 
 /* ─── Konfiguration ──────────────────────────────────────────────── */
 
 #define MQTT_HOST           "localhost"
 #define MQTT_PORT           1883
 #define MQTT_KEEPALIVE      60
 #define CLIENT_ID           "pi5_collector"
 
 /* Verzeichnis für JSON-Datendateien */
 // #define DATA_DIR            "/var/lib/mqtt_collector"
 #define DATA_DIR            "/home/mbo7424/kom_systems/data/mqtt_collector"
 /* Subscribers Übersichtsdatei */
// #define SUBSCRIBERS_FILE    "/home/mbo7424/kom_systems/data/mqtt_collector"
#define SUBSCRIBERS_FILE    "/home/mbo7424/kom_systems/data/mqtt_collector/subscribers.json"
 
/* Topics */
#define TOPIC_SUBSCRIBE_ALL "#"
#define TOPIC_PC_REQUEST    "collector/request"
#define TOPIC_PC_RESPONSE   "collector/response"

#define TOPIC_SUBSCRIBERS_REQUEST  "collector/subscribers/request"
#define TOPIC_SUBSCRIBERS_RESPONSE "collector/subscribers/response"
 /* Shelly Topics: shelly_s/..., shelly_j/..., shelly_x/..., shelly_d/... */
 #define SHELLY_TOPIC_PREFIX "shelly"
 
 /* ESP32 Topics enthalten typischerweise "esp32" oder "sensor" */
 #define ESP32_TOPIC_PREFIX  "esp32"
 
 /* Max. Größe für Datei-Payload beim Senden an PC */
 #define MAX_SEND_SIZE       (512 * 1024)    /* 512 KB */
 
 /* ─── Globale Variablen ──────────────────────────────────────────── */
 
 static volatile int running = 1;
 static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
 static struct mosquitto *mosq = NULL;
 
 /* ─── Funktionen deklarieren ────────────────────────────────────────────── */
 
 // Gibt den aktuellen Zeitstempel als ISO-8601 String zurück.
 static void get_timestamp(char *buf, size_t len) ;
 /* ─── Subscribers Datei aktualisieren ───────────────────────────── */
static void update_subscribers(const char *topic, const char *device_type);
// Erstellt einen sicheren Dateinamen aus einem MQTT-Topic.
 static void topic_to_filename(const char *topic, char *out, size_t out_len);
 
 /**
  * Liest eine komplette Datei in einen heap-allokierten Buffer.
  * Rückgabe: Zeiger auf Buffer (muss mit free() freigegeben werden)
  *           oder NULL bei Fehler.
  */
 static char *read_file(const char *path, long *out_size);
 
 /* ─── Daten speichern ────────────────────────────────────────────── */
 
 /**
  * Shelly Plug S sendet JSON wie:
  *   {"apower": 52.3, "voltage": 229.1, "current": 0.228, "aenergy": {...}}
  */
 static void save_shelly_data(const char *topic, const char *payload, int payloadlen);
 /**
  * ESP32 sendet Rohe Werte, z.B. "23.5" oder "temperature=23.5,humidity=55"
  */
 static void save_esp32_data(const char *topic, const char *payload, int payloadlen);
 
 /* ─── PC-Abfrage Handler ─────────────────────────────────────────── */
 
 /**
  * PC sendet eine Anfrage auf "collector/request".
  * Payload kann leer sein (alle Dateien) oder ein Topic-Filter.
  */
 static void handle_pc_request(const char *payload, int payloadlen);
 
 /* ─── MQTT Callbacks ─────────────────────────────────────────────── */
 
 static void on_connect(struct mosquitto *m, void *userdata, int rc);
 static void on_disconnect(struct mosquitto *m, void *userdata, int rc);
 static void on_message(struct mosquitto *m, void *userdata, const struct mosquitto_message *msg);
 static void on_log(struct mosquitto *m, void *userdata, int level, const char *str);
 
 /* ─── Signal Handler ─────────────────────────────────────────────── */
 
 static void signal_handler(int sig);


 /* ─── Main ───────────────────────────────────────────────────────── */
 
 int main(void) {
     printf("=== MQTT Collector für Raspberry Pi 5 ===\n");
     printf("    Shelly Plug S + ESP32 Datensammler\n\n");
 
     /* Signal Handler */
     signal(SIGINT, signal_handler);
     signal(SIGTERM, signal_handler);
 
     /* Datenverzeichnis erstellen */
     if (mkdir(DATA_DIR, 0755) != 0 && errno != EEXIST) {
         fprintf(stderr, "[ERROR] Kann DATA_DIR nicht erstellen: %s\n", strerror(errno));
         return EXIT_FAILURE;
     }
     printf("[INFO] Datenverzeichnis: %s\n", DATA_DIR);
 
     /* Mosquitto initialisieren */
     mosquitto_lib_init();
     mosq = mosquitto_new(CLIENT_ID, true, NULL);
     if (!mosq) {
         fprintf(stderr, "[ERROR] mosquitto_new() fehlgeschlagen\n");
         return EXIT_FAILURE;
     }
 
     /* Callbacks setzen */
     mosquitto_connect_callback_set(mosq, on_connect);
     mosquitto_disconnect_callback_set(mosq, on_disconnect);
     mosquitto_message_callback_set(mosq, on_message);
     mosquitto_log_callback_set(mosq, on_log);
 
     /* Verbinden */
     int rc = mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, MQTT_KEEPALIVE);
     if (rc != MOSQ_ERR_SUCCESS) {
         fprintf(stderr, "[ERROR] Verbindung zu %s:%d fehlgeschlagen: %s\n",
                 MQTT_HOST, MQTT_PORT, mosquitto_strerror(rc));
         mosquitto_destroy(mosq);
         mosquitto_lib_cleanup();
         return EXIT_FAILURE;
     }
 
     /* Event Loop mit Auto-Reconnect */
     printf("[INFO] Warte auf MQTT-Nachrichten...\n");
     mosquitto_loop_forever(mosq, -1, 1);
 
     /* Aufräumen */
     mosquitto_destroy(mosq);
     mosquitto_lib_cleanup();
     pthread_mutex_destroy(&file_mutex);
 
     printf("[INFO] Beendet.\n");
     return EXIT_SUCCESS;
 }
 



 /* ─── Funktionen definieren ────────────────────────────────────────────── */
 
 /**
  * Gibt den aktuellen Zeitstempel als ISO-8601 String zurück.
  */
 static void get_timestamp(char *buf, size_t len) {
     time_t now = time(NULL);
     struct tm *ptime = localtime(&now);
     strftime(buf, len, "%Y-%m-%dT%H:%M:%S", ptime);
 }
 

 /* ─── Subscribers Datei aktualisieren ───────────────────────────── */
static void update_subscribers(const char *topic, const char *device_type) {
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    // pthread_mutex_lock(&file_mutex);

    /* Vorhandene Datei lesen */
    cJSON *root = NULL;
    char *existing = read_file(SUBSCRIBERS_FILE, NULL);
    if (existing) {
        root = cJSON_Parse(existing);
        free(existing);
    }
    if (!root) {
        root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "subscribers", cJSON_CreateArray());
    }

    cJSON *arr = cJSON_GetObjectItem(root, "subscribers");
    int found = 0;

    /* Topic suchen – falls vorhanden aktualisieren */
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, arr) {
        cJSON *pJTopic = cJSON_GetObjectItem(entry, "topic");
        if (pJTopic && strcmp(pJTopic->valuestring, topic) == 0) {
            /* last_seen und count aktualisieren */
            cJSON_ReplaceItemInObject(entry, "last_seen",
                cJSON_CreateString(timestamp));
            cJSON *cnt = cJSON_GetObjectItem(entry, "message_count");
            if (cnt) cnt->valueint++;
            found = 1;
            break;
        }
    }

    /* Neu anlegen falls nicht gefunden */
    if (!found) {
        cJSON *new_entry = cJSON_CreateObject();
        cJSON_AddStringToObject(new_entry, "topic",        topic);
        cJSON_AddStringToObject(new_entry, "device_type",  device_type);
        cJSON_AddStringToObject(new_entry, "first_seen",   timestamp);
        cJSON_AddStringToObject(new_entry, "last_seen",    timestamp);
        cJSON_AddNumberToObject(new_entry, "message_count", 1);
        cJSON_AddItemToArray(arr, new_entry);
        printf("[SUB]   Neues Gerät: %s (%s)\n", topic, device_type);
    }

    /* last_updated im Root setzen */
    cJSON *plast_update = cJSON_GetObjectItem(root, "last_updated");
    if (plast_update) cJSON_ReplaceItemInObject(root, "last_updated", cJSON_CreateString(timestamp));
    else    cJSON_AddStringToObject(root, "last_updated", timestamp);

    /* Zurückschreiben */
    FILE *pFile = fopen(SUBSCRIBERS_FILE, "w");
    if (pFile) {
        char *out = cJSON_Print(root);
        if (out) { fputs(out, pFile); free(out); }
        fclose(pFile);
    }
    cJSON_Delete(root);
    // pthread_mutex_unlock(&file_mutex);
}

 /**
  * Erstellt einen sicheren Dateinamen aus einem MQTT-Topic.
  * Slashes werden durch Unterstriche ersetzt.
  */
 static void topic_to_filename(const char *topic, char *out, size_t out_len) {
     size_t i;
     snprintf(out, out_len, "%s/", DATA_DIR);
     size_t base_len = strlen(out);
 
     for (i = 0; i < strlen(topic) && (base_len + i) < out_len - 6; i++) {
         char c = topic[i];
         if (c == '/' || c == ' ' || c == ':') {
             out[base_len + i] = '_';
         } else {
             out[base_len + i] = c;
         }
     }
     out[base_len + i] = '\0';
     strncat(out, ".json", out_len - strlen(out) - 1);
 }
 
 /**
  * Liest eine komplette Datei in einen heap-allokierten Buffer.
  * Rückgabe: Zeiger auf Buffer (muss mit free() freigegeben werden)
  *           oder NULL bei Fehler.
  */
 static char *read_file(const char *path, long *out_size) {
     FILE *pFile = fopen(path, "r");
     if (!pFile) return NULL;
 
     fseek(pFile, 0, SEEK_END);
     long size = ftell(pFile);
     rewind(pFile);
 
     char *buf = malloc(size + 1);
     if (!buf) { fclose(pFile); return NULL; }
 
     fread(buf, 1, size, pFile);
     buf[size] = '\0';
     fclose(pFile);
 
     if (out_size) *out_size = size;
     return buf;
 }
 
 /* ─── Daten speichern ────────────────────────────────────────────── */
 
 /**
  * Shelly Plug S sendet JSON wie:
  *   {"apower": 52.3, "voltage": 229.1, "current": 0.228, "aenergy": {...}}
  *
  * Wir lesen die vorhandene Datei, fügen den neuen Eintrag
  * in ein Array ein und schreiben sie zurück.
  */
 static void save_shelly_data(const char *topic, const char *payload, int payloadlen) {
     char filepath[512];
     char timestamp[32];
     topic_to_filename(topic, filepath, sizeof(filepath));
     get_timestamp(timestamp, sizeof(timestamp));
 
     /* Payload parsen */
     cJSON *pIncoming = cJSON_ParseWithLength(payload, payloadlen);
     if (!pIncoming) {
         fprintf(stderr, "[WARN] Shelly JSON Parse-Fehler: Topic=%s\n", topic);
         return;
     }
 
     /* Zeitstempel hinzufügen */
     cJSON_AddStringToObject(pIncoming, "timestamp", timestamp);
     cJSON_AddStringToObject(pIncoming, "topic", topic);
 
     pthread_mutex_lock(&file_mutex);
 
     /* Vorhandene Datei einlesen oder neues Root-Objekt anlegen */
     cJSON *root = NULL;
     char *existing = read_file(filepath, NULL);
     if (existing) {
         root = cJSON_Parse(existing);
         free(existing);
     }
     if (!root) {
         root = cJSON_CreateObject();
         cJSON_AddStringToObject(root, "device_type", "shelly_plug_s");
         cJSON_AddStringToObject(root, "topic", topic);
         cJSON_AddItemToObject(root, "data", cJSON_CreateArray());
     }
 
     /* Neuen Eintrag in Array einfügen */
     cJSON *arr = cJSON_GetObjectItem(root, "data");
     if (arr) cJSON_AddItemToArray(arr, pIncoming);
 
     /* Zurückschreiben */
     FILE *f = fopen(filepath, "w");
     if (f) {
         char *out = cJSON_Print(root);
         if (out) {
             fputs(out, f);
             free(out);
         }
         fclose(f);
         printf("[SHELLY] Gespeichert: %s -> %s\n", topic, filepath);
         update_subscribers(topic, "shelly_plug_s");
     } else {
         fprintf(stderr, "[ERROR] Kann nicht schreiben: %s (%s)\n", filepath, strerror(errno));
         /* incoming gehört jetzt root → nur root löschen reicht */
     }
 
     /* root löscht rekursiv alle Kind-Objekte inkl. incoming */
     cJSON_Delete(root);
     pthread_mutex_unlock(&file_mutex);
 }
 
 /**
  * ESP32 sendet Rohe Werte, z.B. "23.5" oder "temperature=23.5,humidity=55"
  * Wir verpacken den Rohwert in JSON und speichern ihn.
  */
 static void save_esp32_data(const char *topic, const char *payload, int payloadlen) {
     char filepath[512];
     char timestamp[32];
     topic_to_filename(topic, filepath, sizeof(filepath));
     get_timestamp(timestamp, sizeof(timestamp));
 
     /* Rohwert sichern */
     char raw[512];
     int copy_len = payloadlen < (int)(sizeof(raw) - 1) ? payloadlen : (int)(sizeof(raw) - 1);
     memcpy(raw, payload, copy_len);
     raw[copy_len] = '\0';
 
     /* Neuen Eintrag als JSON aufbauen */
     cJSON *entry = cJSON_CreateObject();
     cJSON_AddStringToObject(entry, "timestamp", timestamp);
     cJSON_AddStringToObject(entry, "topic", topic);
 
    // NEU – erst JSON versuchen, dann Rohwert als Fallback:
    cJSON *json_payload = cJSON_Parse(raw);
    if (json_payload) {
        /* JSON erkannt → Felder direkt übernehmen */
        cJSON *item = json_payload->child;
        while (item) {
            cJSON_AddItemToObject(entry, item->string, cJSON_Duplicate(item, 1));
            item = item->next;
        }
        cJSON_Delete(json_payload);
    } else {
        /* Kein JSON → als Zahl oder String speichern */
        char *endptr;
        double numeric = strtod(raw, &endptr);
        if (endptr != raw && *endptr == '\0') {
            cJSON_AddNumberToObject(entry, "value", numeric);
        } else {
            cJSON_AddStringToObject(entry, "value", raw);
        }
    }
 
     pthread_mutex_lock(&file_mutex);
 
     /* Vorhandene Datei einlesen */
     cJSON *root = NULL;
     char *existing = read_file(filepath, NULL);
     if (existing) {
         root = cJSON_Parse(existing);
         free(existing);
     }
     if (!root) {
         root = cJSON_CreateObject();
         cJSON_AddStringToObject(root, "device_type", "esp32");
         cJSON_AddStringToObject(root, "topic", topic);
         cJSON_AddItemToObject(root, "data", cJSON_CreateArray());
     }
 
     cJSON *arr = cJSON_GetObjectItem(root, "data");
     if (arr) cJSON_AddItemToArray(arr, entry);
 
     FILE *f = fopen(filepath, "w");
     if (f) {
         char *out = cJSON_Print(root);
         if (out) {
             fputs(out, f);
             free(out);
         }
         fclose(f);
         printf("[ESP32]  Gespeichert: %s -> %s (%s)\n", topic, filepath, raw);
         update_subscribers(topic, "esp32");
     } else {
         fprintf(stderr, "[ERROR] Kann nicht schreiben: %s (%s)\n", filepath, strerror(errno));
         /* entry gehört jetzt root → nur root löschen reicht */
     }
 
     /* root löscht rekursiv alle Kind-Objekte inkl. entry */
     cJSON_Delete(root);
     pthread_mutex_unlock(&file_mutex);
 }
 
 /* ─── PC-Abfrage Handler ─────────────────────────────────────────── */
 
 /**
  * PC sendet eine Anfrage auf "collector/request".
  * Payload kann leer sein (alle Dateien) oder ein Topic-Filter.
  *
  * Wir lesen alle passenden JSON-Dateien und senden sie
  * gebündelt auf "collector/response".
  */
 static void handle_pc_request(const char *payload, int payloadlen) {
     printf("[PC-REQ] Anfrage erhalten: %.*s\n", payloadlen, payload);
 
     /* Filter aus Payload lesen (leer = alles) */
     char filter[256] = "";
     if (payloadlen > 0 && payloadlen < (int)sizeof(filter)) {
         memcpy(filter, payload, payloadlen);
         filter[payloadlen] = '\0';
     }
 
     /* Alle JSON-Dateien im Datenverzeichnis sammeln */
     char cmd[512];
     snprintf(cmd, sizeof(cmd), "ls %s/*.json 2>/dev/null", DATA_DIR);
 
     FILE *ls = popen(cmd, "r");
     if (!ls) {
         fprintf(stderr, "[ERROR] Kann Verzeichnis nicht lesen: %s\n", DATA_DIR);
         return;
     }
 
     /* Gesamt-Antwort als JSON Array aufbauen */
     cJSON *response = cJSON_CreateObject();
     char ts[32];
     get_timestamp(ts, sizeof(ts));
     cJSON_AddStringToObject(response, "response_time", ts);
     cJSON *files_arr = cJSON_CreateArray();
     cJSON_AddItemToObject(response, "devices", files_arr);
 
     char filepath[512];
     int count = 0;
 
     while (fgets(filepath, sizeof(filepath), ls)) {
         /* Zeilenumbruch entfernen */
         filepath[strcspn(filepath, "\n")] = '\0';
 
         /* Filter anwenden */
        // if (strlen(filter) > 0 && strstr(filepath, filter) == NULL) {
        if (strlen(filter) > 0 && strcmp(filter, "all") != 0 && strstr(filepath, filter) == NULL) {
             continue;
         }
 
         long fsize = 0;
         char *content = read_file(filepath, &fsize);
         if (!content) continue;
 
         cJSON *file_data = cJSON_Parse(content);
         free(content);
 
         if (file_data) {
             cJSON_AddItemToArray(files_arr, file_data);
             count++;
         }
     }
     pclose(ls);
 
     cJSON_AddNumberToObject(response, "device_count", count);
 
     /* Antwort serialisieren und senden */
     char *response_str = cJSON_Print(response);
     cJSON_Delete(response);
 
     if (response_str) {
         int rc = mosquitto_publish(mosq, NULL,
                                    TOPIC_PC_RESPONSE,
                                    strlen(response_str),
                                    response_str, 1, false);
         if (rc == MOSQ_ERR_SUCCESS) {
             printf("[PC-RSP] %d Gerät(e) gesendet auf %s\n", count, TOPIC_PC_RESPONSE);
         } else {
             fprintf(stderr, "[ERROR] MQTT Publish fehlgeschlagen: %s\n",
                     mosquitto_strerror(rc));
         }
         free(response_str);
     }
 }
 
 /* ─── MQTT Callbacks ─────────────────────────────────────────────── */
 
 static void on_connect(struct mosquitto *m, void *userdata, int rc) {
     (void)userdata;
     if (rc == 0) {
         printf("[MQTT]  Verbunden mit Broker %s:%d\n", MQTT_HOST, MQTT_PORT);
         mosquitto_subscribe(m, NULL, TOPIC_SUBSCRIBE_ALL, 1);
         printf("[MQTT]  Subscribed auf: %s\n", TOPIC_SUBSCRIBE_ALL);
     } else {
         fprintf(stderr, "[ERROR] Verbindung fehlgeschlagen: %s\n",
                 mosquitto_connack_string(rc));
     }
 }
 
 static void on_disconnect(struct mosquitto *m, void *userdata, int rc) {
     (void)m; (void)userdata;
     if (rc != 0) {
         printf("[MQTT]  Verbindung getrennt (rc=%d), versuche Neuverbindung...\n", rc);
     }
 }
 
 static void on_message(struct mosquitto *m, void *userdata,
                         const struct mosquitto_message *msg) {
     (void)m; (void)userdata;
     // if (!msg->payload || msg->payloadlen == 0) return;
     if (!msg->payload) return;
 
     const char *topic = msg->topic;
     const char *payload = (const char *)msg->payload;
     int payloadlen = msg->payloadlen;
 
     /* PC-Anfrage? */
     if (strcmp(topic, TOPIC_PC_REQUEST) == 0) {
         handle_pc_request(payload, payloadlen);
         return;
     }
        /* Subscribers-Anfrage? */
    if (strcmp(topic, TOPIC_SUBSCRIBERS_REQUEST) == 0) {
        char *content = read_file(SUBSCRIBERS_FILE, NULL);
        if (content) {
            mosquitto_publish(mosq, NULL, TOPIC_SUBSCRIBERS_RESPONSE,
                            strlen(content), content, 1, false);
            printf("[SUB-RSP] subscribers.json gesendet\n");
            free(content);
        }
        return;
    }
     /* Eigene Response-Topics ignorieren */
     if (strncmp(topic, "collector/", 10) == 0) {
         return;
     }
 
     /* Shelly Status-Pings und RPC ignorieren (kein Messwert) */
     if (strstr(topic, "/online") != NULL ||
         strstr(topic, "/rpc")    != NULL ||
         strstr(topic, "/events") != NULL) {
         return;
     }
 
     /* Gerät identifizieren */
     if (strstr(topic, SHELLY_TOPIC_PREFIX) != NULL) {
         /* Nur status/switch Topics speichern (echte Messdaten) */
         if (strstr(topic, "status/switch") != NULL) {
             save_shelly_data(topic, payload, payloadlen);
         }
     } else if (strstr(topic, ESP32_TOPIC_PREFIX) != NULL) {
         save_esp32_data(topic, payload, payloadlen);
     } else {
         /* Unbekanntes Gerät → als Rohwert behandeln */
         printf("[UNKNOWN] Topic: %s | Payload: %.*s\n", topic, payloadlen, payload);
         save_esp32_data(topic, payload, payloadlen);
     }
 }
 
 static void on_log(struct mosquitto *m, void *userdata, int level, const char *str) {
     (void)m; (void)userdata;
     if (level == MOSQ_LOG_ERR || level == MOSQ_LOG_WARNING) {
         fprintf(stderr, "[MOSQ] %s\n", str);
     }
 }
 
 /* ─── Signal Handler ─────────────────────────────────────────────── */
 
 static void signal_handler(int sig) {
     (void)sig;
     printf("\n[INFO] Beende Programm...\n");
     running = 0;
     if (mosq) mosquitto_disconnect(mosq);
 }
 