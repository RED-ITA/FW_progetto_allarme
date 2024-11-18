# Guida per Testare il Programma con curl su Windows

Questa guida ti fornirà istruzioni passo passo su come testare il tuo programma utilizzando comandi `curl` dal terminale su Windows.

## Prerequisiti

- **curl** installato sul tuo sistema Windows.
- Il tuo dispositivo (ad esempio, ESP32) è connesso alla rete ed esegue il programma server HTTP.
- Conosci l'indirizzo IP del dispositivo su cui è in esecuzione il server.

## Passo 1: Verificare l'Installazione di curl

### Controllare se curl è già installato

Apri il prompt dei comandi e digita:

```cmd
curl --version
```

Se `curl` è installato, vedrai la versione installata. Se ricevi un messaggio di errore, procedi all'installazione.

### Installare curl su Windows

A partire da Windows 10 versione 1803, `curl` è preinstallato. Se hai una versione precedente o `curl` non è disponibile, puoi scaricarlo seguendo questi passaggi:

1. Vai al sito ufficiale di [curl per Windows](https://curl.se/windows/).
2. Scarica la versione appropriata per il tuo sistema (32-bit o 64-bit).
3. Estrai l'archivio scaricato in una cartella sul tuo computer.
4. Aggiungi il percorso della cartella estratta alla variabile di ambiente `PATH`:

   - Vai su **Pannello di Controllo** > **Sistema e sicurezza** > **Sistema**.
   - Clicca su **Impostazioni di sistema avanzate**.
   - Nella scheda **Avanzate**, clicca su **Variabili d'ambiente**.
   - Nella sezione **Variabili di sistema**, seleziona la variabile `Path` e clicca su **Modifica**.
   - Clicca su **Nuovo** e inserisci il percorso della cartella dove hai estratto `curl`.
   - Clicca su **OK** per salvare le modifiche.

## Passo 2: Ottenere l'Indirizzo IP del Dispositivo

Per comunicare con il tuo dispositivo, devi conoscere il suo indirizzo IP.

- Se il tuo dispositivo mostra l'indirizzo IP sulla porta seriale al momento dell'avvio, puoi ottenerlo da lì.
- In alternativa, puoi utilizzare strumenti di scansione di rete come `Advanced IP Scanner` o `nmap` per trovare il dispositivo sulla tua rete locale.

## Passo 3: Testare una Richiesta HTTP GET

Verifica che il server HTTP sia attivo eseguendo una semplice richiesta GET.

Apri il prompt dei comandi e digita:

```cmd
curl http://<indirizzo_ip_del_dispositivo>/
```

Sostituisci `<indirizzo_ip_del_dispositivo>` con l'indirizzo IP effettivo del tuo dispositivo.

Se il server è in esecuzione, dovresti ricevere una risposta, come ad esempio il contenuto della pagina principale.

## Passo 4: Inviare Dati con una Richiesta HTTP POST

Supponiamo che il tuo server accetti credenziali Wi-Fi tramite una richiesta POST all'endpoint `/wifi-config`.

### Preparare i Dati da Inviare

Crea un file di testo chiamato `data.json` contenente le credenziali:

```json
{
    "ssid": "Wind3 HUB - A81FC0",
    "password": "45xhk7qhtytkyzb6"
}
```



Assicurati di salvare il file nella directory corrente o specifica il percorso completo.

### Inviare la Richiesta POST

Esegui il seguente comando nel prompt dei comandi:

```cmd
curl -X POST http://<indirizzo_ip_del_dispositivo>/wifi-config -H "Content-Type: application/json" -d @data.json
```

- `-X POST` specifica che stai effettuando una richiesta POST.
- `-H "Content-Type: application/json"` imposta l'header `Content-Type` a `application/json`.
- `-d @data.json` indica che i dati da inviare si trovano nel file `data.json`.

### Alternative senza File

Se preferisci non utilizzare un file, puoi inserire i dati direttamente nel comando:

```cmd
curl -X POST http://192.168.4.1/wifi-config -H "Content-Type: application/json" -d "{\"ssid\":\"Wind3 HUB - A81FC0\",\"password\":\"45xhk7qhtytkyzb6\"}"
curl -X POST http://192.168.4.1/wifi_config -H "Content-Type: application/x-www-form-urlencoded" -d "ssid=Wind3 HUB - A81FC0&password=45xhk7qhtytkyzb6"

```

Assicurati di eseguire correttamente l'escaping delle virgolette doppie.

## Passo 5: Verificare la Risposta del Server

Dopo aver inviato la richiesta POST, il server dovrebbe rispondere con un messaggio di conferma.

Esempio di risposta attesa:

```json
{
    "status": "success",
    "message": "SSID ricevuto e salvato"
}
```

Se ricevi un messaggio di errore, controlla i log del dispositivo per ulteriori dettagli.

## Passo 6: Verificare che le Credenziali siano Salvate

Per assicurarti che le credenziali siano state salvate correttamente, puoi implementare un endpoint sul tuo server che restituisce le credenziali salvate (solo per scopi di test; in produzione, evita di esporre le credenziali).

Esempio di richiesta:

```cmd
curl http://<indirizzo_ip_del_dispositivo>/wifi-credentials
```

## Passo 7: Ulteriori Test

- **Testare la Connessione Wi-Fi**: Dopo aver salvato le credenziali, il dispositivo dovrebbe tentare di connettersi alla rete Wi-Fi specificata. Verifica che la connessione avvenga correttamente.
- **Gestire Errori**: Prova a inviare dati errati o incompleti per testare come il server gestisce le richieste non valide.

## Passo 8: Debug e Risoluzione dei Problemi

- **Controllare i Log Seriali**: Se il dispositivo è collegato tramite USB, puoi monitorare l'output seriale per diagnosticare eventuali problemi.
- **Verificare la Connettività di Rete**: Assicurati che il tuo computer e il dispositivo siano sulla stessa rete e che non ci siano firewall che bloccano le richieste.

## Note Aggiuntive

- **Formati dei Dati**: Assicurati che i dati inviati siano nel formato atteso dal server (ad esempio, JSON).
- **Persistenza dei Dati**: Verifica che i dati salvati nell'NVS persistano anche dopo un riavvio del dispositivo.

## Risorse Utili

- [Documentazione di curl](https://curl.se/docs/manpage.html)
- [ESP-IDF HTTP Server Example](https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server)
- [Utilizzo dell'NVS su ESP32](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)

---

Con questa guida dovresti essere in grado di testare il tuo programma utilizzando `curl` dal terminale su Windows. Se incontri problemi, verifica attentamente ogni passo e assicurati che il tuo dispositivo sia correttamente configurato e connesso.

---