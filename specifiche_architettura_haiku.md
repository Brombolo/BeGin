# Specifica di Architettura per Assistente LLM: Applicazione Modulare e Dinamica per Haiku OS

Questo documento definisce l'architettura software, le linee guida di implementazione e i vincoli di sicurezza per lo sviluppo di un'applicazione nativa in C++ per **Haiku OS**. Il documento è strutturato per essere fornito a un LLM di coding (es. Google Antigravity) al fine di guidarlo nella generazione di codice sicuro, idiomatico e aderente alle API del sistema operativo.

---

## 1. Obiettivo Generale e Requisiti Chiave
L'obiettivo è realizzare un'applicazione host modulare (estendibile tramite add-on `.so`) altamente dinamica, integrata con lo scripting di sistema e progettata per garantire massima stabilità e sicurezza della memoria.

### Punti Cardine:
* **Linguaggio e Toolchain:** C++ standard (GCC nativo su Haiku).
* **Interfaccia Utente:** Nativa Haiku (`BApplication`, `BWindow`, `BView`), layout lineare con navigazione a schede (Tab) verticali.
* **Modularità Runtime:** Caricamento e scaricamento a caldo degli add-on tramite scansione di cartelle e monitoraggio dei nodi (`Node Monitor`).
* **Automazione Estensibile:** Supporto completo allo scripting di sistema tramite `BPropertyInfo` per rispondere nativamente al comando `hey`.
* **SDK e Replicanti:** Creazione di un kit di sviluppo con classi base e supporto alla tecnologia `BReplicant` per l'integrazione nella Scrivania (Tracker).
* **Sicurezza del Codice:** Prevenzione rigorosa di memory leak, puntatori appesi (dangling pointers) e crash di sistema dovuti a moduli malfunzionanti.

---

## 2. Architettura della Finestra Contenitore Globale (Host)

La finestra principale (`MainWindow`, derivata da `BWindow`) funge da coordinatore centrale ed è suddivisa in due macro-aree principali:

### 2.1 Layout dell'Interfaccia Grafica
Il layout deve essere gestito tramite il sistema di layout nativo di Haiku (`BGroupLayout`, `BSplitView`).
* **Barra di Navigazione Laterale (Sinistra):** Un'area verticale che ospita una pila di icone (pulsanti grafici o `BBitmap`). Ogni icona rappresenta un modulo caricato. L'aggiunta di un modulo inserisce dinamicamente una nuova icona in fondo alla pila.
* **Area dei Contenuti (Centro/Destra):** Rappresenta la porzione dominante dello spazio. È gestita tramite una vista contenitore (es. una `BView` con un layout a schede o in cui si sostituisce dinamicamente la vista attiva). Al click su un'icona laterale, la vista centrale si aggiorna mostrando il contenuto specifico di quel modulo.

```
+-------------------------------------------------------+
|  MainWindow (BWindow)                                 |
+-------------------------------------------------------+
| [Icona M1] |  Area Contenuto Centrale (Dominante)    |
| [Icona M2] |                                         |
| [Icona M3] |  Mostra la BView del modulo selezionato |
|            |                                         |
| (Pila vert)|                                         |
+------------+------------------------------------------+
```

### 2.2 Gestione Dinamica dei Moduli (Runtime Add-ons)
L'applicazione non deve avere moduli hard-coded. All'avvio e durante l'esecuzione, scansiona una specifica directory (es. `/boot/home/config/settings/MiaApp/add-ons/`).

* **Scansione Iniziale:** Ciclo sulla cartella usando `BDirectory` e `BEntry` per individuare i file `.so`.
* **Caricamento:** Uso delle funzioni POSIX/Haiku `load_add_on()` per caricare il codice binario in memoria e recuperare il simbolo di istanziazione (es. una funzione di factory come `instantiate_module`).
* **Node Monitoring:** Configurazione del `BNodeMonitor` sulla cartella dei moduli.
  * **Evento `B_ENTRY_CREATED`:** L'app verifica se l'entry è un `.so` valido, lo carica a runtime, istanzia il modulo, aggiunge l'icona alla barra laterale e aggiorna l'UI senza riavviare.
  * **Evento `B_ENTRY_REMOVED`:** L'app individua quale modulo corrisponde al file rimosso, distrugge in sicurezza le istanze grafiche associate, rimuove l'icona dalla barra laterale, chiama `unload_add_on()` e pulisce la memoria per evitare blocchi o crash ("viste appese").

---

## 3. Scripting Nativo e Integrazione con `hey`

L'applicazione e i suoi moduli devono esporre le proprie funzionalità al sistema di scripting di Haiku. Questo permette agli utenti e agli script Bash di interrogare l'applicazione tramite il comando `hey`.

### 3.1 Il Centralino: `MainWindow` / `BApplication`
* Riceve i messaggi di scripting globali dal sistema.
* Implementa `ResolveSpecifier()` per intercettare i target del messaggio. Se l'utente digita un comando indirizzato a un modulo (es. `hey MiaApp Module "NomeModulo" GET Status`), l'host estrae lo specifier, individua il modulo associato e reindirizza il `BMessage` al `BHandler` di quel modulo.

### 3.2 Il Contratto di Scripting: `BPropertyInfo`
Ogni entità scriptabile (l'applicazione madre e ciascun modulo figlio) deve definire internamente un array di strutture `property_info`.
* La tabella definisce le proprietà supportate (es. `"Disable"`, `"Status"`, `"Value"`), i tipi di messaggi accettati (`B_GET_PROPERTY`, `B_SET_PROPERTY`, `B_EXECUTE_PROPERTY`) e una stringa descrittiva di aiuto (`HELP`).
* All'interno di `ResolveSpecifier()`, si utilizza `fPropertyInfo->FindMatch(...)` per convalidare il comando in entrata. Se c'è corrispondenza, l'oggetto risponde `this` (dichiarandosi target); altrimenti delega alla classe base.
* Il messaggio viene elaborato nel metodo `MessageReceived(BMessage* message)` intercettando i codici dei messaggi di proprietà ed estraendo i parametri generici passati nel `BMessage`.

---

## 4. Struttura dell'SDK e Sviluppo dei Moduli

L'SDK verrà isolato in una cartella dedicata (es. `sdk/`) e conterrà gli header necessari e un progetto d'esempio per gli sviluppatori terzi.

### 4.1 La Classe Base: `BaseModule`
Ogni modulo deve ereditare da una classe architetturale comune fornita dall'SDK:

```cpp
class BaseModule : public BHandler {
public:
    BaseModule(const char* name, const char* signature);
    virtual ~BaseModule();

    // Interfaccia Grafica obbligatoria
    virtual BView* GetInterfaceView() = 0;
    virtual BBitmap* GetIcon() = 0;

    // Integrazione Scripting standard
    virtual BHandler* ResolveSpecifier(BMessage* message, int32 index,
                                        BMessage* specifier, int32 form,
                                        const char* property) override;
    virtual void       MessageReceived(BMessage* message) override;

protected:
    BPropertyInfo* fPropertyInfo;
};
```

### 4.2 Replicanti (`BReplicant`)
Per rendere i moduli ancora più integrati, l'SDK deve prevedere l'opzione (ove la natura del modulo lo consenta) di implementare il supporto ai Replicanti.
* La `BView` restituita dal modulo può sovrascrivere `Archive(BMessage* archive, bool deep)` e includere la proprietà `class` corretta per consentire al Tracker (o ad altri contenitori come la Scrivania) di istanziare la vista in modo indipendente dall'applicazione principale.
* Questo richiede che il file `.so` sia compilato esportando la funzione globale `BView* instantiate_replicant_view(BMessage* archive)`.

---

## 5. Linee Guida per la Sicurezza e la Stabilità della Memoria

Scrivere software per Haiku richiede rigore nella gestione delle risorse, poiché i moduli vengono eseguiti nello stesso spazio di indirizzamento dell'applicazione. Un crash del modulo non deve trascinare con sé l'applicazione.

### 5.1 Regole Tassative per l'LLM:
1. **Gestione della memoria RAII:** Utilizzare smart pointer (`std::unique_ptr`, `std::shared_ptr`) per la gestione delle risorse interne dei moduli che non sono direttamente legate al ciclo di vita delle viste di Haiku.
2. **Ciclo di vita delle `BView`:** Quando una `BView` viene aggiunta a una finestra (`AddChild()`), la finestra ne assume la proprietà e la distrugge automaticamente quando viene distrutta. Quando si rimuove un modulo a runtime, la vista deve essere rimossa esplicitamente tramite `RemoveChild()` prima di essere eliminata in sicurezza, per evitare eccezioni nell'interfaccia.
3. **Puntatori Orfani nel Node Monitor:** Quando viene scaricato un add-on (`unload_add_on`), assicurarsi che TUTTI i riferimenti a oggetti allocati da quella libreria (inclusi i `BHandler` registrati nel looper dell'applicazione e le `BView` nell'interfaccia) siano stati precedentemente rimossi e distrutti. Chiamare `unload_add_on` con oggetti ancora attivi distruggerà la tabella dei simboli virtuali (vtable), causando un crash immediato (`Segmentation Fault`) al successivo accesso.
4. **Validazione dei Messaggi di Scripting:** Non fidarsi mai del contenuto di un `BMessage` proveniente da `hey`. Controllare sempre l'esistenza e il tipo dei campi estratti (usando i codici di ritorno di `FindString`, `FindInt32`, ecc.) prima di utilizzarli nel flusso logico del programma.
5. **Thread Safety:** Le modifiche all'interfaccia utente guidate da eventi esterni (come il Node Monitor o lo Scripting) devono sempre avvenire previa acquisizione del lock della finestra corretta (`LockLooper()` / `UnlockLooper()`).

---

## 6. Prossimi Passi del Piano di Sviluppo

L'assistente LLM dovrà seguire rigorosamente questa roadmap sequenziale:
1. **Fase 1:** Definizione del codice sorgente per l'applicazione Host (`MainWindow` e logica iniziale di layout a schede fisse).
2. **Fase 2:** Implementazione del `Node Monitor` e della logica di caricamento/scaricamento dinamico dei file `.so`.
3. **Fase 3:** Definizione della classe `BaseModule` dell'SDK e della prima implementazione di `BPropertyInfo` per lo scripting.
4. **Fase 4:** Ottimizzazione grafica delle icone/tab verticali e validazione del supporto ai Replicanti.
