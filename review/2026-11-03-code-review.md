# Drosera – Revisione tecnica (DSP, real-time, VST3/AU, build)

Data: 2026-11-03. Commit analizzato: `9447f73` (branch `claude/audio-plugin-vst3-au-review-9b137d`).
Revisione eseguita su una copia isolata del progetto; il repository originale non è stato modificato.
Gli esperimenti e le patch di prova sono in `review/experiments/`. I log di build e test citati sono stati tenuti fuori dal repository.

---

## 1. Executive summary

Drosera è un drone synth a feedback (porting del firmware Audrey II) in JUCE 8.0.9, con formati VST3, AU e
Standalone. Il progetto è in buono stato: la separazione fra motore DSP (senza JUCE) e wrapper plugin è
netta, il thread audio non alloca né blocca, la suite di test è ampia (471 verifiche offline, test del VST3
caricato in un host, test dell'interfaccia, checksum di riferimento del motore) e tutto è passato nella
sandbox. Il manuale documenta onestamente i limiti sonori.

Nessun problema Critical o High. I punti più rilevanti:

| ID | Severità | Sintesi |
|---|---|---|
| D-01 | Medium | Overflow intero con segno (undefined behaviour) nel generatore di rumore DaisySP, eseguito a ogni campione sul thread audio. Rilevato da UBSan. Fix di una riga, bit-identico. |
| D-02 | Medium | L'echo parte da 5 s e scivola al valore della manopola in ~3 s dopo ogni `prepareToPlay` (e dopo ogni recupero da NaN): le prime ripetizioni sono in ritardo e intonate male. Ereditato dal firmware. |
| D-03 | Medium | L'ordine dei gestori dei parametri dipende dall'iterazione di `std::unordered_map`: i render di riferimento (checksum) dipendono dalla libreria standard, e la compensazione di fase MIDI legge valori del tick corrente o precedente a seconda di quell'ordine. |
| D-04 | Low | Latenza dichiarata all'host ma nessun `processBlockBypassed`: in bypass l'audio passa senza ritardo (compensazione di latenza dell'host disallineata). |
| D-05 | Low | L'Audio Unit viene costruito ma mai validato (`auval`) né rilasciato; la CI valida solo il VST3. |
| D-06 | Low | `prepareToPlay` con sample rate 0 produce un `ratio` infinito convertito a `size_t`. |

CPU misurata: da 0,5 % (mono) a 2,2 % (tre voci MIDI) di un core Apple Silicon. Memoria: circa 3,5 MB di
stato DSP per istanza.

Valutazione complessiva: qualità alta per un progetto di questa dimensione; gli interventi suggeriti sono
piccoli e a basso rischio. Vedi §12 per il piano.

---

## 2. Architettura analizzata

### 2.1 Componenti

| Area | File | Ruolo |
|---|---|---|
| Motore (senza JUCE) | `src/dsp/FeedbackSynthEngine.*` | Tre `FeedbackVoice`, rumore bianco per voce, echo stereo, accoppiamento fra voci, rampa di polifonia |
| | `src/dsp/FeedbackVoice.*` | Un anello di feedback: linea di ritardo stereo ("Body"), due corde Karplus-Strong, overdrive, LPF/HPF biquad, riverbero `ReverbSc`, band-pass di tracking opzionale |
| | `src/dsp/KarplusString.*`, `BiquadFilters.*`, `EchoDelay.h`, `StereoEcho.h` | Blocchi DSP |
| | `src/dsp/DroseraCore.h` | Facciata a 48 kHz fissi: mappa parametri normalizzati, smoothing a 12 kHz (blocco di 4 campioni), MIDI pitch/gate, polifonia, PONG, limiter, recupero da NaN |
| | `src/dsp/ParameterRegistry.h`, `SmoothedValue.h` | Registro parametri con smoothing one-pole e handler `std::function` |
| | `src/dsp/SincResampler.h` | Convertitore windowed-sinc (96 tap, Kaiser β=9, 512 fasi) |
| | `src/dsp/VoiceAllocator.h` | Assegnazione note MIDI a tre voci, array fissi |
| Plugin | `src/plugin/PluginProcessor.cpp` | `AudioProcessor`: APVTS (25 parametri), MIDI sample-accurate, ponte di sample rate, stato, programmi |
| | `src/plugin/PluginEditor.*`, `src/plugin/ui/*` | Finestra 760×500, pianta/spettro, manopole, tab |
| | `src/plugin/SpectrumTap.h` | FIFO SPSC lock-free audio → UI |
| | `src/plugin/EditorBridge.h` | Interfaccia editor → processor oltre ai parametri |
| | `src/plugin/Presets.h` | 16 preset di fabbrica |
| Terze parti | `third_party/DaisySP` (commit 7f67768, MIT) | Delay line, ReverbSc, Limiter, Overdrive, WhiteNoise, Tone, DcBlock, CrossFade |
| Test | `tests/render_test.cpp` (1930 righe), `plugin_host_test.cpp`, `ui_test.cpp`, `voice_allocator_test.cpp`, `render_cli.cpp`, `editor_snapshot.cpp` | |

### 2.2 Flusso del segnale

```
host in (L+R)/2 · inputgain ──► [SincResampler host→48k] ──┐
                                                           ▼
   per voce v:  noise_v + in + coupling_v ──► DelayLine(Body, R = L−4) ──► KarplusString ×2 ──► Overdrive
                ──► LPF12 ──► HPF12 ──► ReverbSc (mix) ──► samp_v ──► ×fb_gain ──► [BPF24 tracking] ──► DelayLine (write)
   mix voci (×ramp, ×1/√3) ──► StereoEcho (send; PONG: linee incrociate, drive, tone, width, level) ──► 0.5·(dry+wet)
   ──► ×volume ──► Limiter (pre-gain 0.7) ──► controllo NaN/inf (Recover) ──► [SincResampler 48k→host]
   ──► hard clip ±1 ──► SpectrumTap (UI) ──► host out
```

Il controllo gira a 12 kHz: ogni 4 campioni `ParameterRegistry::Process` applica lo smoothing e chiama gli
handler, che scrivono nel motore. In modo MIDI/LOCK `applyPitchAndBody` calcola la risposta in fase dei
filtri dell'anello alla nota (numeri complessi) e corregge i ritardi perché l'anello oscilli sulla
fondamentale.

### 2.3 Threading

- Thread audio: `processBlock` → `updateParams` (legge gli atomici APVTS) → `renderChunk` → `DroseraCore::Render`.
- Thread messaggi: editor con timer a 30 Hz; legge solo atomici (`published_note_`, `midiPitchActive_`, `uiTab`…) e la FIFO `SpectrumTap`.
- `prepareToPlay` alloca (vettori, tabelle del resampler, `Engine::Init` alla prima chiamata); mai dal thread audio.
- Stato: XML tramite `copyXmlToBinary`; `replaceState` è thread-safe (documentato in JUCE).

---

## 3. Ambiente di analisi

Sandbox: `…/scratchpad/sandbox/` con `src/` (copia da `git archive HEAD`), `tools/venv/` (strumenti), `build/release`, `build/asan`, `experiments/`. Nessuna installazione globale sull'host.

| Strumento | Versione | Provenienza |
|---|---|---|
| macOS | Darwin 27.2.0, arm64 | host |
| Compilatore | AppleClang 21.0.0.21000401 (Command Line Tools) | host, già presente |
| CMake | 4.4.4 | venv della sandbox (pip) – **mancava sull'host** |
| Ninja | 1.13.2 | venv della sandbox (pip) – **mancava sull'host** |
| JUCE | 8.0.9 | scaricato da CMake `FetchContent` nella build dir della sandbox |
| clang-tidy | LLVM 23.1.4 | venv della sandbox (pip) |
| Python | 3.11 (uv venv) | per `tools/test_release_check.py` |

Non disponibili e non installati: `pluginval` (richiede download di un binario esterno), `auval` su un
componente non installato in `~/Library/Audio/Plug-Ins/Components` (fuori dalla sandbox), toolchain Intel
(l'host è arm64; la CI costruisce l'universale).

Modifiche sperimentali, **solo nella copia sandbox**:
1. `CMakeLists.txt`: aggiunto il target `host_probe` (vedi `review/experiments/CMakeLists-host_probe.txt`).
2. `tests/host_probe.cpp`: nuovo (copia in `review/experiments/`).
3. `third_party/DaisySP/Source/Noise/whitenoise.h`: patch per D-01 (`review/experiments/whitenoise-unsigned.patch`), applicata **dopo** la prima esecuzione dei sanitizer, per verificare il fix.
4. Riconfigurazione con `CMAKE_EXPORT_COMPILE_COMMANDS=ON` per clang-tidy.

---

## 4. Configurazione e compilazione

| Build | Esito |
|---|---|
| Release, Ninja, tutti i target (VST3, AU, Standalone, test) | OK, 199 passi, 53 warning |
| Debug + `-fsanitize=address,undefined` (render_test, voice_allocator_test, render_cli, ui_test) | OK |
| Firme ad hoc dei bundle (`codesign --verify --deep --strict`) | OK per VST3, AU, Standalone |
| AU `Info.plist` | `aumu` / `Dros` / `Cdsk`, versione 257 (0.1.1), bundle `com.codeskine.Drosera` |
| Binario | arm64, minOS 11.0; VST3 e AU ≈ 10 MB ciascuno (statico JUCE) |

Dei 53 warning, 44 vengono da DaisySP (compilato con `-w`, ma gli header sono inclusi dal plugin con i
flag JUCE) e 9 dal codice del progetto: `ParameterRegistry.h` (shadowing dei campi nel costruttore, `;`
superflui), `DroseraCore.h:52` (conversione di segno), `PluginProcessor.cpp:243` (`!=` fra float, qui
intenzionale), `PluginProcessor.cpp:50` (`switch-enum`). Nessuno indica un bug.

---

## 5. Risultati dei test

### 5.1 Eseguiti con successo

| Test | Risultato |
|---|---|
| `voice_allocator_test` | 14/14 OK |
| `render_test` (Release) | 471 verifiche OK, 0 FAIL; allocazioni durante il render (incluso recupero da NaN): 0 |
| `ui_test` (Release) | ALL OK; allocazioni nel percorso audio polifonico: 0 (hook `malloc_logger`) |
| `plugin_host_test` sul VST3 costruito, con fixture `state-baseline.bin` | ALL OK: identità dei 25 parametri e dei loro ID VST3, stato salvato dalla build baseline, round trip dello stato, livelli a 44.1/48/96 kHz × blocchi 64/512/1000, input audio, MIDI gate, latenza dichiarata = misurata (±3 campioni), nota MIDI 60 entro +0,2 cent a tutte le frequenze di campionamento, preset |
| `tools/hardware-comparison/check_reference.sh` | 10/10 WAV con SHA-256 identico ai riferimenti |
| `tools/test_release_check.py` | 8 test OK |
| `voice_allocator_test` sotto ASan+UBSan | OK, nessun report |
| `render_test` e `ui_test` sotto ASan+UBSan | Prima esecuzione: **interrotte al primo campione** dall'overflow in `whitenoise.h:34` (D-01). Con la patch di D-01 applicata alla copia sandbox: `ui_test` ALL OK e `render_test` 471/471 OK, **zero report** di ASan e UBSan in entrambi. |
| `host_probe` (esperimento, VST3 nell'host JUCE) | ALL OK: 22.05/44.1/88.2/176.4/192 kHz × blocchi 1/7/2048/8192 con input; latenza dichiarata = misurata a 22.05/88.2/192 kHz (±5); 60 s a 44.1 kHz con blocchi misti (4096, 64, 1000, 3, 2048, 512) senza buchi; blocco vuoto; bypass; stato corrotto e troncato; note-on con velocity 0 |
| Esperimenti DSP (`exp1_cpu`, `exp2_edges`, `exp3_order`, `exp4_size`) | Vedi §7, §8, §9 |

### 5.2 Falliti

Nessun test del progetto è fallito. L'unico esito negativo è il report UBSan di D-01, che impedisce di
usare i sanitizer così com'è.

### 5.3 Non eseguibili per limiti ambientali

- `pluginval` (la CI lo esegue a strictness 5 sul VST3): richiede il download di un eseguibile esterno.
- `auval` sull'AU: richiede l'installazione del componente in `~/Library`, fuori dalla sandbox.
- Build e test Intel x86_64: host arm64. I checksum di riferimento sono dichiarati validi solo su arm64.
- ThreadSanitizer: non eseguito (richiederebbe una terza build; il percorso audio↔UI è stato verificato a lettura, §8).

### 5.4 Non previsti dal progetto

- Validazione AU in CI (D-05).
- Test a sample rate ≠ 44.1/48/96 kHz e a blocchi > 1000 campioni (coperti qui da `host_probe`, tutti OK).
- Build con sanitizer in CI.

---

## 6. Problemi individuati (per severità)

Convenzione: file e righe si riferiscono al commit analizzato.

### D-01 · Medium · Real-time / Bug
**File:** `third_party/DaisySP/Source/Noise/whitenoise.h:34` (`randseed_ *= 16807;`, `int32_t`)
**Descrizione:** moltiplicazione intera con segno che va in overflow a quasi ogni chiamata: comportamento
indefinito in C++. Viene eseguita per ogni campione da tre generatori (`Engine::Process`) e 60 020 volte in
`Engine::Init` (pre-roll dei semi).
**Impatto:** oggi i compilatori producono il wrap-around atteso, quindi il suono è corretto; ma il codice non
ha garanzie, un futuro compilatore o flag (`-ftrapv`, ottimizzazioni su UB) può cambiarne l'esito, e **oggi
rende inutilizzabili ASan/UBSan** sull'intera suite (si fermano al primo campione).
**Evidenza:** `runtime error: signed integer overflow: 282475249 * 16807 cannot be represented in type 'int'`,
stack `WhiteNoise::Process ← Engine::Init ← DroseraCore::Init ← prepareToPlay`.
**Soluzione:** `randseed_ = (int32_t) ((uint32_t) randseed_ * 16807u);` – stessi bit, stesso suono.
Verificato nella sandbox: con la patch, `check_reference.sh` dà 10/10 checksum identici e `render_test` passa
(471/471), quindi il suono non cambia di un bit. Registrare la modifica in `third_party/DaisySP/PATCHES.txt` (regola 4).
**Priorità:** 1 (banale, sblocca i sanitizer).

### D-02 · Medium · DSP
**File:** `src/dsp/FeedbackSynthEngine.cpp:34-36` (`SetDelayTime(5.0f, true)`, `SetLagTime(0.5f)`), `src/dsp/EchoDelay.h:114-118`
**Descrizione:** `Engine::Init` imposta il tempo dell'echo a 5 s "immediatamente"; il valore reale arriva dal
registro al tick successivo ma come *target*, e `EchoDelay` lo insegue con un one-pole da 0,5 s. Il tempo
di ritardo quindi scivola da 5 s al valore della manopola nei primi secondi dopo **ogni** `prepareToPlay`
(caricamento, cambio di sample rate o buffer in molti host) e dopo ogni `Recover()`.
**Impatto:** le prime ripetizioni sono molto in ritardo e, poiché il ritardo varia mentre si legge, trasposte
verso il basso (effetto nastro). Dopo circa 3 s tutto è a posto. È comportamento del firmware, dove però
`Init` avviene solo all'accensione.
**Evidenza:** `exp2_edges`, sezione A, echo time 0,248 s: impulso a t=0 → prima ripetizione dopo 0,954 s;
a t=2 s → 0,296 s; a t=6 s → 0,248 s.
**Soluzione:** al primo push (`first_`) e dopo `Recover`, far saltare anche lo stato interno dell'echo:
`Engine::SetEchoDelayTime(float, bool immediate)` → `stereo_echo_->SetDelayTime(t, immediate)`, chiamato
da `DroseraCore::pushControls` con `imm`. Cambia i render di riferimento con echo: va dichiarato e
vanno rigenerati i checksum (regola 2).
**Nota emersa applicando il fix** (`review/experiments/exp5_stall.cpp`): il one-pole in `float` di `EchoDelay` non
raggiunge mai il suo target: si ferma quando il passo scende sotto mezzo ulp, cioè 8,6 campioni prima di 0,248 s,
68,7 prima di 1,29 s, 137 prima di 2,5 s (fino a 2,9 ms). Per questo i tre render con echo cambiano per tutta la
durata, non solo nei primi secondi. Lo stesso scarto resta, dopo il fix, a ogni movimento della manopola Echo Time:
inudibile (0,1 %), ma se si vuole esattezza basta agganciare il valore al target quando la distanza è sotto un
campione (opzionale, cambierebbe ancora i checksum).
**Priorità:** 3.

### D-03 · Medium · Architecture (portabilità, determinismo)
**File:** `src/dsp/ParameterRegistry.h:67-71` e `:99`, `src/dsp/DroseraCore.h:141` (commento che ammette il problema), `:206-227`
**Descrizione:** gli handler vengono chiamati nell'ordine di iterazione di `std::unordered_map<int, …>`, che
è un dettaglio della libreria standard. Con libc++ è l'inverso dell'ordine di registrazione (`exp3_order`:
`OutputVolume … FeedbackBody FeedbackGain Frequency`). L'handler di `Frequency` chiama `applyPitchAndBody(0)`,
che usa `body_s_`, `lpf_hz_`, `hpf_hz_` scritti dagli handler di Body/LPF/HPF: con l'ordine attuale vede i
valori del tick corrente, con un altro ordine quelli del tick precedente. Inoltre, ogni tick fa una ricerca
hash e una chiamata `std::function` per parametro.
**Impatto:** (a) i checksum di riferimento valgono solo per questa libreria standard (oltre che per arm64);
(b) un build con libstdc++ (Linux/Windows, citato in `BUILDING.md` come possibile ripresa) renderebbe
diverso senza che nulla lo segnali; (c) costo per tick evitabile.
**Evidenza:** `review/experiments/exp3_order.cpp`; `DroseraCore.h:141` ("more entries could reorder its handlers").
**Soluzione:** sostituire la mappa con `std::array<ParamState, N>` indicizzato dall'enum e iterare in un
ordine scelto esplicitamente. Mantenendo l'ordine inverso attuale i render restano identici; in
alternativa dichiarare il cambio. Elimina `kParamSmoothingThresh` (inutilizzato) e il TODO. ~40 righe,
riduce codice.
**Priorità:** 2.

### D-04 · Low · Compatibility
**File:** `src/plugin/PluginProcessor.cpp:168-170` (latenza dichiarata), nessun override di `processBlockBypassed`
**Descrizione:** con host ≠ 48 kHz e bus d'ingresso collegato il plugin dichiara una latenza (327 campioni a
44,1 kHz, 1264 a 192 kHz); il bypass di default di JUCE copia l'ingresso in uscita senza ritardo e, in
Debug, scatta `jassert(getLatencySamples() == 0)` (`juce_AudioProcessor.cpp:599`).
**Impatto:** durante il bypass, l'audio passante è spostato in avanti di quella latenza rispetto alla
compensazione dell'host; in Debug l'assert interrompe. Riguarda solo l'uso come effetto su un ingresso.
**Soluzione:** override di `processBlockBypassed` con una linea di ritardo di `getLatencySamples()` campioni
dimensionata in `prepareToPlay` (`juce::dsp::DelayLine` o un ring di `std::vector` già allocato). ~25 righe.
**Priorità:** 4.

### D-05 · Low · Compatibility (AU)
**File:** `CMakeLists.txt:67` (`FORMATS VST3 AU Standalone`), `.github/workflows/ci.yml` (target `Drosera_VST3`, `pluginval` solo VST3)
**Descrizione:** l'AU viene costruito da ogni build locale, ma la CI non lo compila, non esegue `auval` né
`pluginval` su di esso, e la release pubblica solo il VST3.
**Impatto:** l'AU è un artefatto non verificato; chi lo installa da una build locale non ha garanzie
(Logic/GarageBand rifiutano i componenti che non passano `auval`).
**Soluzione:** o rimuovere `AU` dai formati finché non è supportato, oppure aggiungere alla CI:
build `Drosera_AU`, copia in `~/Library/Audio/Plug-Ins/Components`, `killall -9 AudioComponentRegistrar`,
`auval -v aumu Dros Cdsk`, `pluginval` sul `.component`.
**Priorità:** 5.

### D-06 · Low · Bug (robustezza)
**File:** `src/plugin/PluginProcessor.cpp:142-150`
**Descrizione:** `ratio = 48000 / sampleRate`; con `sampleRate` 0 (alcuni host interrogano/preparano con
valori provvisori) `ratio` è infinito, `std::ceil(inf)` convertito a `size_t` è comportamento indefinito e
`outL.assign(...)` può chiedere una quantità enorme di memoria.
**Impatto:** crash o eccezione `bad_alloc` all'apertura in un host che passa 0.
**Soluzione:** all'inizio di `prepareToPlay`: `if (sampleRate <= 0.0) sampleRate = DroseraCore::kRate;`. 1 riga.
**Priorità:** 6.

### D-07 · Low · Bug (UX) / Compatibility
**File:** `src/dsp/DroseraCore.h:192` (clamp a `kNoteMin..kNoteMax` tramite `Update`), `src/plugin/PluginProcessor.cpp:403-404` (`Interval` −24..+24)
**Descrizione:** la nota di una voce è limitata a 16..84; con la manopola Frequency in alto (nota 72) un
intervallo di +24 st suona la nota 84, come +12. Lo stesso in basso.
**Evidenza:** `exp2_edges`, sezione D: voce 1 (+24) e voce 2 (+12) pubblicano entrambe 84,0.
**Impatto:** la manopola mostra "+24 st" ma la voce non lo suona; nessun artefatto.
**Soluzione:** nel readout dell'intervallo mostrare l'intervallo effettivo (nota pubblicata − nota della
voce 0), oppure documentarlo nel manuale. Non cambiare il range del parametro (romperebbe le automazioni).
**Priorità:** 9.

### D-08 · Low · Real-time
**File:** `src/dsp/DroseraCore.h:125-129` (`Recover`), `FeedbackSynthEngine.cpp:13-37`
**Descrizione:** il recupero da NaN re-inizializza tutto il motore sul thread audio: azzera 3 `ReverbSc`
(386 KB l'uno), le due linee dell'echo (940 KB l'una), corde e delay (≈ 160 KB per voce): ≈ 3,5 MB scritti
in un colpo, oltre a riportare l'echo a 5 s (D-02).
**Impatto:** un picco di carico di qualche centinaio di µs nel blocco in cui scatta; con buffer da 64
campioni a 96 kHz (0,67 ms) può causare un dropout. È un percorso d'emergenza: in 3 minuti di render ai
valori massimi (`exp2_edges` B) non è mai scattato, e il test `recoversFromNan` ne verifica l'esito.
**Soluzione:** accettabile così; se si vuole, azzerare solo il necessario (delay line, filtri, riverberi) senza
ricalcolare i semi del rumore, e saltare subito l'echo al valore corrente.
**Priorità:** 10 (opzionale).

### D-09 · Low · Real-time / Compatibility (thread)
**File:** `src/plugin/PluginProcessor.cpp:103-130`
**Descrizione:** `setCurrentProgram` scrive `apvts.state.setProperty(...)` (un `ValueTree`, non thread-safe) e
chiama `setValueNotifyingHost` con gesture. Dall'editor è sul thread messaggi; un host AU che ripristina un
preset da un altro thread farebbe una corsa con i listener dell'APVTS.
**Soluzione:** eseguire la parte `ValueTree` sul thread messaggi (`MessageManager::callAsync`) o almeno
`jassert(MessageManager::existsAndIsCurrentThread())`. ~5 righe.
**Priorità:** 11.

### D-10 · Low · Security (supply chain) / Build
**File:** `CMakeLists.txt:48-51`
**Descrizione:** JUCE è scaricato con `GIT_TAG 8.0.9`. Un tag git è mutabile.
**Soluzione:** usare l'hash del commit del tag (con `GIT_SHALLOW` serve un server che supporti fetch per
SHA, GitHub lo fa) o verificare un hash dell'archivio. 1 riga.
**Priorità:** 7.

### D-11 · Low · Bug (MIDI)
**File:** `src/plugin/PluginProcessor.cpp:158-159`
**Descrizione:** `prepareToPlay` azzera `heldCount`, `haveNote` e l'allocatore: una nota tenuta mentre l'host
cambia buffer o sample rate si perde (con MIDI Gate il suono si chiude finché non si suona un'altra nota).
Mancano anche il sustain pedal (CC 64) e la bend range configurabile (RPN), oggi fissa a ±2 st.
**Impatto:** raro nell'uso normale; i secondi due sono limiti di funzionalità, non bug.
**Soluzione:** conservare `held[]` e riapplicare `allocator.Assign(held, heldCount)` dopo `Init`; valutare
CC 64 e RPN 0 come funzionalità.
**Priorità:** 12.

### D-12 · Low · Code quality
Warning nel codice del progetto (§4) e segnalazioni clang-tidy senza impatto: `performance-unnecessary-value-param`
per `Handler` in `ParameterRegistry.h:39,87` (copia di `std::function` alla registrazione, solo in `Init`),
`bugprone-implicit-widening-of-multiplication-result` in `PluginProcessor.cpp:147,150` e `FeedbackSynthEngine.h:77`
(valori piccoli, innocui), `cert-err33-c` su `snprintf` in `Readouts.h:14`. Correggere quando si tocca il file.
**Priorità:** 13.

---

## 7. Analisi DSP e qualità audio

**Motore a 48 kHz fissi.** Scelta corretta per preservare il comportamento del firmware (dimensioni delle
delay line, `ReverbSc` tarato su 48 kHz) e la riproducibilità dei checksum. Il costo è il resampler e la sua
latenza, dichiarata solo con l'ingresso collegato (coerente: senza ingresso non c'è nulla da allineare).

**Resampler windowed-sinc.** Misure (`exp2_edges` C, 48 k → 44,1 k): guadagno 0,00 dB a 1, 10 e 18 kHz;
−16,5 dB a 21 kHz (banda di transizione, sotto la nuova Nyquist di 22,05 kHz); −94 dB a 23 kHz. Nessun
aliasing udibile, tabella normalizzata a guadagno unitario in DC. Il ponte input-driven/output-driven con
prime di 256 campioni ha retto 60 s di blocchi misti da 3 a 4096 campioni senza buchi; la latenza
misurata dopo quella prova è 329 campioni contro 327 dichiarati: nessuna deriva del FIFO.

**Stabilità numerica.** L'anello è limitato da `fclamp(±20)` nella corda, `SoftClip` nell'overdrive e nel
echo, `Limiter` in uscita; `juce::ScopedNoDenormals` nel `processBlock` copre i denormali sul thread audio
(i test offline non lo attivano, ma non hanno mostrato rallentamenti). Ai valori massimi di tutto
(reverb decay 1,0, feedback +12 dB, echo feedback 150 %, PONG drive +12 dB, coupling 10 %) 3 minuti di
render restano finiti, picco ≤ 0,82, zero recuperi (`exp2_edges` B). Il controllo NaN/inf per blocco con
`Recover` è una rete di sicurezza ragionevole per un sistema caotico.

**Headroom e clipping.** Il `Limiter` DaisySP usa `SoftLimit`, che non è limitato a ±1 sui transienti; il
`jlimit(−1, 1)` finale nel `processBlock` è quindi giustificato. Nei 3 minuti ai massimi non si è mai
superato 1,0 dopo il limiter. Il `0.5·(dry+wet)` dell'echo dimezza sempre il segnale diretto: è del
firmware, non un errore.

**Filtri.** Biquad TDF-II a singola precisione con coefficienti bilineari standard (`BiquadFilters.cpp`),
Q 0,9 per LPF/HPF, cutoff limitato a `[1, fs/2]`. Il tracking band-pass (BPF24, Q 1,5) e la compensazione di
fase complessa in `applyPitchAndBody` sono corretti: la nota 60 misura +0,1/+0,2 cent attraverso il VST3 a
44,1/48/96 kHz. Lo smoothing della nota (t60 0,2 s) produce un portamento su ogni cambio di nota MIDI: è
coerente col carattere del firmware ma vale la pena sapere che non è configurabile.

**Aliasing.** La saturazione del PONG non è sovracampionata; il manuale lo dichiara (−54 dB di potenza
inarmonica al massimo Drive). Per un echo "a nastro" è una scelta accettabile; un oversampling 2× sul solo
`SoftClip` di `ReadShaped` costerebbe poco (Medium complessità) se in futuro si volesse ripulirlo.

**Continuità al cambio di parametri.** Tutto passa dal registro con smoothing a 12 kHz (nessun zipper);
le rampe di polifonia e PONG (20 ms) e il blend di tuning (50 ms) sono verificati click-free dai test.
Eccezione: D-02 all'avvio.

**Sound design.** Le scelte creative (Body che "snappa" ai periodi della nota, Coupling con make-up,
WARP come doppler) sono deliberate e documentate nei design spec in `docs/superpowers/`. Non le classifico
come problemi. Il manuale elenca con onestà i limiti (note 16–18 e 80 mute, reverb che stona le note
MIDI, chord drift con Body alto).

---

## 8. Problemi real-time

Verifica del percorso `processBlock` → `renderChunk` → `DroseraCore::Render` → `Engine::Process`:

| Aspetto | Esito |
|---|---|
| Allocazioni | Nessuna: vettori/tabelle allocati in `prepareToPlay`; `Engine::Init` alloca solo alla prima chiamata; `MidiMessage` costruito solo per messaggi ≤ 3 byte (i sysex allocherebbero, ed è evitato). Confermato da `render_test` (operator new contato: 0) e `ui_test` (`malloc_logger`: 0). |
| Lock | Nessuno. `SpectrumTap` è SPSC lock-free con acquire/release corretti; la UI legge solo atomici. |
| I/O, logging | Nessuno sul thread audio. |
| Determinismo | Nessuna chiamata a tempo variabile; `std::pow/tan/atan2/log2` sono costanti nel tempo. |
| Denormali | `ScopedNoDenormals`. |
| UB | D-01 (overflow nel rumore). |
| Costo in casi rari | `Recover()` (D-08). |
| Comunicazione UI ↔ audio | Parametri via atomici APVTS letti una volta per blocco; `published_note_`/`published_body_s_` atomici relaxed; `uiTab`, `currentProgram`, `hostRate` atomici; `setStateInformation` usa `replaceState` (thread-safe, non RT, mai dal thread audio). `setCurrentProgram` vedi D-09. |
| Race `prepared` | `bool` non atomico scritto in `prepareToPlay`/`releaseResources` e letto in `processBlock`: accettabile perché gli host serializzano queste chiamate, ma un `std::atomic<bool>` costa nulla. |

---

## 9. Ottimizzazioni di performance

Misure (`exp1_cpu`, 20 s di audio a 48 kHz, blocchi da 512, un core M-series, Release `-O3`):

| Modalità | CPU |
|---|---|
| Default (mono, manopole) | 0,53 % |
| Keys (mono, tracking MIDI) | 0,91 % |
| Open Fifths (poly, LOCK) | 2,13 % |
| Sympathetic (poly, libero) | 1,35 % |
| Chord Keys (poly MIDI) | 2,19 % |
| Ember Pong (PONG) | 0,59 % |

`render_test` riporta 9 ms per secondo di audio mono e 21 ms poly. A questo si aggiunge il resampler a
host ≠ 48 kHz (96 tap × 3 canali ≈ 300 MAC/campione, trascurabile). Memoria per istanza: `Engine` 475 KB
inline + 3 × `ReverbSc` 386 KB + echo 1,9 MB + tabelle del resampler 0,6 MB ≈ 3,5 MB.

**Conclusione:** nessuna correzione necessaria. Ottimizzazioni *opzionali*, in ordine di resa:

1. **Non ricalcolare i coefficienti a ogni tick** (`DroseraCore.h:212-215, 243-248`, `FeedbackVoice::SetFeedbackTracking`).
   Oggi ogni 4 campioni ogni handler scrive nel motore anche se il valore è fermo: `tanf` per LPF/HPF ×3
   voci, 4 `tanf` per i BPF24 di tracking per voce, `expf` per il livello PONG, più `pow`, `polar`, `atan2`,
   `log2` in `applyPitchAndBody`. Rimedio: in `SmoothedValue::get` restituire anche "cambiato" (confronto
   bit a bit, dopo la convergenza il one-pole è esattamente stabile) e saltare l'handler. Beneficio stimato:
   −30/50 % sulla parte di controllo (che è una frazione del 2 %). Rischio: l'ordine/dipendenza fra handler
   (D-03) va risolto prima, perché `applyPitchAndBody` dipende da più parametri. Complessità media.
2. **`applyPitchAndBody(0)` chiamato due volte per tick** (handler di Frequency e di Body, `DroseraCore.h:207, 211`):
   basta una chiamata dopo entrambi. Beneficio piccolo, rischio nullo se l'ordine è esplicito (D-03).
3. **Registro senza hash e `std::function`** (parte di D-03): array indicizzato dall'enum, handler come
   puntatore a membro o `switch`. Beneficio piccolo; vale per il determinismo più che per la CPU.
4. **SIMD**: non consigliato. L'anello è ricorsivo campione per campione (feedback a ritardo frazionario),
   non vettorizzabile; i tre voci sono indipendenti solo entro il campione. Il resampler è già un prodotto
   scalare che il compilatore vettorizza da solo.

---

## 10. Problemi architetturali e manutenibilità

**Punti di forza.** Confine netto DSP/JUCE con test senza JUCE; `EditorBridge` tiene l'editor lontano dal
processor; `Presets.h`, `Readouts.h`, `SpectrumBands.h` testabili da soli; proprietà chiara (`unique_ptr`
per gli oggetti grandi, `Engine` non copiabile); nessun `new` nudo; ID parametri con `versionHint` per
l'ordinamento AU; regole di progetto scritte (`CLAUDE.md`, `CONTRIBUTING.md`) e design spec per ogni
funzionalità; CI con pluginval.

**Da migliorare.**
- `ParameterRegistry` (D-03): la generalità del template e della mappa non è usata (tre registri con enum
  fissi); un array la sostituisce con meno codice e più garanzie.
- `DroseraCore.h` è un header di 368 righe con matematica non banale (`applyPitchAndBody`,
  `blendedStringNote`) e tre registri: resta leggibile grazie ai commenti, ma le funzioni di risposta in
  frequenza (`stringLoopResponse`, `biquadResponse`) replicano a mano i coefficienti di `BiquadFilters.cpp` e
  di `Tone`/`DcBlock`: se uno cambia, l'altro va aggiornato a mano. Un commento incrociato o una funzione
  condivisa che restituisca i coefficienti eviterebbe la deriva.
- `PluginProcessor.cpp` tiene insieme layout dei parametri, stato MIDI (`held[]`, allocatore, bend), ponte
  di sample rate e programmi: 475 righe ancora gestibili, ma lo stato MIDI (`handleMidi`, `applyMidiState`,
  `removeNote`) è un candidato naturale a una piccola classe testabile senza JUCE, come `VoiceAllocator`.
- Cache di `std::atomic<float>*` per 25 parametri: funziona, ma un array indicizzato da un enum dei
  parametri plugin ridurrebbe le 15 righe di `getRawParameterValue` ripetute.
- `kSpecs` in `PluginProcessor.cpp` duplica range e curve di `DroseraCore::registerParams` ("Ranges/curves
  mirror…"): una tabella condivisa nel DSP eviterebbe la duplicazione.
- Il commento in `FeedbackVoice.h:21-22` ("Init leaves fb_delay_samp_…") e quello in `Engine::Init` sono
  essenziali perché `Init` serve anche da reset sul thread audio: convenzione delicata ma documentata.

---

## 11. Compatibilità VST3 / AU

| Aspetto | Stato |
|---|---|
| Parametri | 25 parametri 0..1 con `ParameterID` versionati (1 e 2), nomi in inglese, formattazione testo coerente con il motore; test d'identità ID/nome che blocca regressioni. Nel VST3 l'host vede 2107 parametri: i 25 + Bypass + 2080 di emulazione MIDI CC/aftertouch/pitch-bend (`JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS`, default JUCE per i synth). Serve per ricevere il pitch bend in VST3; è normale per i synth JUCE, ma ingombra le liste di automazione degli host. Nessuna azione. |
| Automazione | Letta una volta per blocco, poi smoothing interno a 12 kHz: nessun zipper, niente automazione sample-accurate (standard JUCE). |
| Stato | XML in binario, tag verificato, parametri assenti → default (progetti vecchi caricano, test con fixture baseline), `uiTab` e `program` salvati. Stato corrotto o troncato ignorato senza crash (`host_probe`). |
| Programmi | 16 preset come programmi; `setCurrentProgram` imposta i parametri notificando l'host; indice persistito. Vedi D-09. |
| Bypass | Default JUCE; D-04 quando c'è latenza. |
| Bus | Uscita solo stereo; ingresso disabilitato/mono/stereo. Un host che chieda un'uscita mono non istanzia: accettabile per un synth. L'ingresso mono è letto dal solo canale 0 (corretto, il canale 1 del buffer può contenere spazzatura). |
| MIDI | Note on/off (velocity 0 = off), all notes/sound off, pitch wheel ±2 st, omni. Sysex ignorati senza allocare. Sample-accurate. D-11. |
| Sample rate e buffer | Verificato 22,05–192 kHz, blocchi 1–8192 e 0, con latenza dichiarata corretta. |
| Doppia precisione | Non supportata (dichiarato correttamente). |
| Tail | 10 s dichiarati; il drone è auto-oscillante, il valore è nominale. |
| AU | Bundle corretto (`aumu`/`Dros`/`Cdsk`, versione 257); mai validato (D-05). |
| Standalone | Costruito e firmato; non eseguito (richiede un dispositivo audio). |
| macOS | minOS 11.0; firma ad hoc rifatta dopo la build (corretto, perché JUCE aggiunge `moduleinfo.json` dopo la firma); la CI firma e notarizza se ci sono i segreti. |

---

## 12. Piano di intervento prioritizzato

| # | Intervento | ID | Tipo | Complessità | Effetto sui checksum |
|---|---|---|---|---|---|
| 1 | Moltiplicazione senza segno in `whitenoise.h` + nota in `PATCHES.txt`; aggiungere un job CI con ASan/UBSan su `render_test` e `ui_test` | D-01 | Correzione necessaria | Banale | Nessuno (verificato) |
| 2 | `ParameterRegistry` su array con ordine esplicito | D-03 | Correzione consigliata | Bassa (~40 righe, meno codice) | Nessuno se si conserva l'ordine attuale |
| 3 | Echo che salta subito al valore della manopola all'avvio e dopo `Recover` | D-02 | Correzione consigliata (udibile) | Bassa | Sì: rigenerare con motivazione |
| 4 | `processBlockBypassed` con ritardo pari alla latenza | D-04 | Correzione consigliata | Bassa | Nessuno |
| 5 | Validare l'AU in CI (`auval`, `pluginval`) o togliere `AU` dai formati | D-05 | Consigliata | Bassa (CI) | Nessuno |
| 6 | Clamp del sample rate in `prepareToPlay` | D-06 | Correzione (1 riga) | Banale | Nessuno |
| 7 | Pin di JUCE all'hash del commit | D-10 | Consigliata | Banale | Nessuno |
| 8 | `std::atomic<bool> prepared` | §8 | Opzionale | Banale | Nessuno |
| 9 | Readout dell'intervallo effettivo o nota nel manuale | D-07 | Opzionale | Banale | Nessuno |
| 10 | Recupero da NaN più leggero | D-08 | Opzionale | Media | Nessuno (solo dopo NaN) |
| 11 | `setCurrentProgram` sul thread messaggi | D-09 | Opzionale | Bassa | Nessuno |
| 12 | Note tenute attraverso `prepareToPlay`; sustain pedal; RPN bend range | D-11 | Funzionalità | Bassa/Media | Nessuno |
| 13 | Skip degli handler a valore fermo; una sola `applyPitchAndBody` per tick | §9 | Ottimizzazione opzionale | Media | Nessuno se fatto bit a bit |
| 14 | Pulizia warning e clang-tidy | D-12 | Opzionale | Banale | Nessuno |

---

## 13. Interventi applicati (dopo la revisione)

Su richiesta, gli interventi 1–7 del piano sono stati applicati nel worktree (non committati). Diff: 13 file,
+144/−56 righe.

| # | ID | Cosa è cambiato | Verifica |
|---|---|---|---|
| 1 | D-01 | `whitenoise.h`: prodotto in `uint32_t` riconvertito a `int32_t`; nota in `PATCHES.txt` | 7 render senza echo identici al bit; `ui_test` e `voice_allocator_test` passano sotto ASan+UBSan senza report; `render_test` sotto ASan+UBSan: 472/472 OK, zero report |
| 2 | D-03 | `ParameterRegistry` su `std::array<std::optional<ParamState>, N>`, `Process` scorre dall'id più alto al più basso (l'ordine che libc++ dava); gli enum hanno `NumVoiceParams`/`NumEchoParams`; −9 righe nette | I 7 render senza echo restano identici al bit; i 3 con echo cambiano solo per D-02 |
| 3 | D-02 | `Engine::SetEchoDelayTime(t, immediate)`; `DroseraCore` fa saltare l'echo al primo tick dopo `Init` e dopo `Recover` (`echo_jump_`); nuovo test `echoStartsAtKnobTime` | Prima ripetizione a 0,248 s con manopola a 0,248 s (prima: 0,954 s). `SHA256SUMS` rigenerati: p05, p06, p10 cambiano, gli altri 7 no. Motivo nel CHANGELOG |
| 4 | D-04 | `processBlockBypassed` con un ring per canale lungo `getLatencySamples()`, allocato in `prepareToPlay`; nuovo test `bypassKeepsLatency`/`bypassPassesThrough` | Bypass a 44,1 kHz: dichiarati 327, misurati 327; a 48 kHz passa inalterato |
| 5 | D-05 | CI: build di `Drosera_AU`, installazione nella cartella Components del runner, `auval -v aumu Dros Cdsk`, `pluginval` sull'AU | Non eseguibile qui (vedi §3); va osservato al primo run della CI |
| 6 | D-06 | `prepareToPlay`: sample rate ≤ 0 → 48 000 | Compila; comportamento a rate validi invariato (`plugin_host_test` ALL OK) |
| 7 | D-10 | `GIT_TAG` di JUCE = hash del commit di 8.0.9 | Il configure del worktree ha scaricato JUCE per hash con `GIT_SHALLOW ON` |

Suite complete sul worktree dopo le modifiche: `voice_allocator_test` OK, `render_test` 472 verifiche OK,
`ui_test` ALL OK, `plugin_host_test` ALL OK (identità parametri, fixture baseline, latenza), `check_reference.sh`
10/10 con i nuovi checksum. `tests/fixtures/state-baseline.bin` non è stato toccato.

Restano aperti gli opzionali 8–14 del piano e la nota sullo scarto residuo del one-pole dell'echo (in D-02).

---

## 14. Misure per il sound design

Le misure automatiche su livelli dei preset, tessitura MIDI, ripetibilità del caricamento, compatibilità mono,
durata, combinazioni estreme e sample rate sono in `review/2026-11-03-sound-design-measurements.md`. I due
punti da portare nel piano: lo stesso progetto può assestarsi su una parziale diversa a seconda di quando
l'host consegna lo stato (rimedio: salto immediato dei parametri al ripristino), e il riverbero nell'anello
stona le note MIDI sotto il 48 fino a ±46 cent al 35 % di Mix.

---

## Valutazione complessiva e roadmap

Il progetto è solido: il thread audio è pulito, il DSP è corretto e misurato (intonazione MIDI entro
0,2 cent, resampler a −94 dB in banda attenuata, stabilità per minuti ai valori estremi), la persistenza è
protetta da fixture, e le scelte sonore sono documentate con i loro limiti. I difetti trovati sono di
contorno: un UB ereditato da una libreria vendorizzata (che oggi nasconde la possibilità di usare i
sanitizer), un transitorio d'avvio ereditato dal firmware, un ordine di valutazione lasciato alla libreria
standard, e un AU costruito ma non verificato.

Roadmap suggerita:
- **Subito (una sessione):** 1, 6, 7, 8 — correzioni banali senza effetto sui render — più il job CI con sanitizer.
- **Prossima release:** 2 e 3 (con rigenerazione dei checksum motivata nel changelog), 4, 5.
- **Quando si tocca l'area:** 9–14.

Non controllato: pluginval e auval (vincoli della sandbox), build Intel, ThreadSanitizer, ascolto
soggettivo dei preset.
