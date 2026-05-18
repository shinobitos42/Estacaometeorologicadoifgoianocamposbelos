#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <DHT.h> 

// =====================================================
// 1. CONFIGURAÇÕES DA REDE WI-FI E FIREBASE
// =====================================================
#define WIFI_SSID "Loira do banheiro"
#define WIFI_PASSWORD "arrozemanueca"
#define FIREBASE_HOST "estacao-fisica-lab-if-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "KsBqqQ89BK5C5AYl2BoKSdQUNSyII2qQmyRfvBdN"

// =====================================================
// 2. PINAGEM DOS SENSORES
// =====================================================
const int pinoLDR = A0;      // Sensor de Luz no A0
#define DHTPIN D5            // Sensor de Umidade DHT11 no D5
#define DHTTYPE DHT11        
DHT dht(DHTPIN, DHTTYPE);

const int PINO_CHUVA = D6;   // Sensor Hall da Chuva no D6

// Barômetro BMP280 (I2C automático nos pinos D1 e D2)
Adafruit_BMP280 bmp; 

// =====================================================
// VARIÁVEIS DOS SENSORES VARIADOS
// =====================================================
int valorLDR = 0;
String ultimaCondicaoSalva = ""; 

float pressao = 0.0;
float temperaturaBMP = 0.0;
float altitude = 0.0; 
float umidade = 0.0; 

unsigned long tempoAnteriorSensores = 0;
const unsigned long intervaloSensores = 5000; // Leitura geral a cada 5 segundos

unsigned long tempoAnteriorWiFi = 0;
const unsigned long intervaloWiFi = 5000;

// =====================================================
// VARIÁVEIS DA CHUVA (SUA LOGICA DO SEU CODIGO ANTIGO)
// =====================================================
volatile int cliquesChuva = 0; 
volatile unsigned long tempoUltimoChuva = 0;
volatile bool atualizarChuvaFirebase = false;

const unsigned long cooldownChuva = 150; // Seu debounce original de 150ms
const float MILIMETROS_POR_BASCULA = 0.25; // Cada clique = 0.25 mm de chuva
float chuvaAcumulada = 0.0;

String horaUltimoClique = "--:--:--";
int ultimoDia = 0;

// =====================================================
// INSTÂNCIAS DO FIREBASE
// =====================================================
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

// =====================================================
// DECLARAÇÃO DE FUNÇÕES
// =====================================================
void conectarWiFi();
void configurarFirebase();
void sincronizarHora();
void enviarAoFirebase();
void salvarNoHistorico();
void salvarResumoDiario();
void verificarResetDaMeiaNoite();
String obterHoraAtual();
String obterDataAtual();
String obterCategoriaLDR(int v);

// =====================================================
// INTERRUPÇÃO DA CHUVA (RÁPIDA E EFICIENTE)
// =====================================================
void ICACHE_RAM_ATTR contarChuva() {
  unsigned long tempoAtual = millis();
  if (tempoAtual - tempoUltimoChuva > cooldownChuva) {
    cliquesChuva++;
    tempoUltimoChuva = tempoAtual;
    atualizarChuvaFirebase = true;
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  Wire.begin(); 

  // Configuração da Interrupção do Sensor Hall de chuva
  pinMode(PINO_CHUVA, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PINO_CHUVA), contarChuva, FALLING);
  Serial.println("\n[OK] Sensor de Chuva ativo no pino D6!");

  // Inicialização do DHT11
  dht.begin();
  Serial.println("[OK] Sensor DHT11 iniciado!");
  
  // Inicialização do BMP280
  Serial.println("Procurando Barometro BMP280...");
  if (!bmp.begin(0x76) && !bmp.begin(0x77)) {
    Serial.println("[ERRO CRITICO] BMP280 nao encontrado!");
  } else {
    Serial.println("[OK] BMP280 iniciado com sucesso!");
  }

  conectarWiFi();
  sincronizarHora();
  configurarFirebase();

  // Leituras iniciais base
  valorLDR = analogRead(pinoLDR);
  ultimaCondicaoSalva = obterCategoriaLDR(valorLDR);

  time_t agora = time(nullptr);
  struct tm* timeInfo = localtime(&agora);
  ultimoDia = timeInfo->tm_mday;

  Serial.println("\n=================================");
  Serial.println(" ESTAÇÃO METEOROLÓGICA UNIFICADA ");
  Serial.println("=================================");
}

// =====================================================
// LOOP PRINCIPAL
// =====================================================
void loop() {
  verificarResetDaMeiaNoite();

  // Controle de reconexão do Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long tempoAtual = millis();
    if (tempoAtual - tempoAnteriorWiFi >= intervaloWiFi) {
      tempoAnteriorWiFi = tempoAtual;
      Serial.println("[WIFI] Reconectando...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  unsigned long tempoAtual = millis();

  // =================================================
  // EVENTO DE CHUVA INTERRUPÇÃO (DISPARA NA HORA QUE CHOVE)
  // =================================================
  if (atualizarChuvaFirebase && WiFi.status() == WL_CONNECTED) {
    atualizarChuvaFirebase = false;
    horaUltimoClique = obterHoraAtual();
    chuvaAcumulada = cliquesChuva * MILIMETROS_POR_BASCULA;

    Serial.println("\n[EVENTO CHUVA] Clique detectado! Enviando dados imediatos...");
    
    enviarAoFirebase();
    salvarNoHistorico();
  }

  // =================================================
  // LEITURA PERIÓDICA DOS SENSORES (A CADA 5 SEGUNDOS)
  // =================================================
  if (tempoAtual - tempoAnteriorSensores >= intervaloSensores) {
    tempoAnteriorSensores = tempoAtual;
    
    // Atualiza variáveis dos sensores estáticos
    valorLDR = analogRead(pinoLDR);
    pressao = bmp.readPressure() / 100.0F;
    temperaturaBMP = bmp.readTemperature();
    altitude = bmp.readAltitude(1013.25);
    umidade = dht.readHumidity();
    chuvaAcumulada = cliquesChuva * MILIMETROS_POR_BASCULA;

    // Output de diagnóstico no Monitor Serial
    Serial.println("\n--- LEITURA COMPLETA ---");
    Serial.print("Luminosidade : "); Serial.print(valorLDR); Serial.println(" (Bruto)");
    Serial.print("Temperatura  : "); Serial.print(temperaturaBMP); Serial.println(" *C");
    if (isnan(umidade)) {
      Serial.println("Umidade      : Erro DHT11!");
    } else {
      Serial.print("Umidade      : "); Serial.print(umidade); Serial.println(" %");
    }
    Serial.print("Pressao Atm  : "); Serial.print(pressao); Serial.println(" hPa");
    Serial.print("Altitude     : "); Serial.print(altitude); Serial.println(" m");
    Serial.print("Chuva Acumul.: "); Serial.print(chuvaAcumulada); Serial.println(" mm");
    Serial.print("Total Cliques: "); Serial.println(cliquesChuva);
    Serial.println("------------------------");

    // Envio Periódico para Tempo Real no Painel
    if (WiFi.status() == WL_CONNECTED) {
      enviarAoFirebase(); 
    }

    // Histórico por alteração climática perceptível (LDR)
    String condicaoAtual = obterCategoriaLDR(valorLDR);
    if (condicaoAtual != ultimaCondicaoSalva) {
      Serial.print("[MUDANÇA CLIMÁTICA] Céu alterou para: ");
      Serial.println(condicaoAtual);

      if (WiFi.status() == WL_CONNECTED) {
        salvarNoHistorico();
        ultimaCondicaoSalva = condicaoAtual;
      }
    }
  }

  delay(10);
}

// =====================================================
// FUNÇÕES AUXILIARES E CONEXÕES
// =====================================================
String obterCategoriaLDR(int v) {
  if (v < 200) return "Muito escuro";
  if (v < 400) return "Nublado";
  if (v < 700) return "Parcialmente nublado";
  return "Plena luz solar";
}

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando ao WiFi");
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500); Serial.print("."); tentativas++;
  }
  if (WiFi.status() == WL_CONNECTED) Serial.println("\nWiFi conectado!");
}

void sincronizarHora() {
  configTime(-3 * 3600, 0, "pool.ntp.br", "time.google.com");
  Serial.print("Sincronizando relogio");
  int tentativas = 0;
  while (time(nullptr) < 1000000000l && tentativas < 20) {
    delay(500); Serial.print("."); tentativas++;
  }
  Serial.println("\nRelogio sincronizado!");
}

void configurarFirebase() {
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

String obterHoraAtual() {
  time_t agora = time(nullptr);
  struct tm* timeInfo = localtime(&agora);
  char buffer[9];
  sprintf(buffer, "%02d:%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  return String(buffer);
}

String obterDataAtual() {
  time_t agora = time(nullptr);
  struct tm* timeInfo = localtime(&agora);
  char buffer[11];
  sprintf(buffer, "%02d/%02d/%04d", timeInfo->tm_mday, timeInfo->tm_mon + 1, timeInfo->tm_year + 1900);
  return String(buffer);
}

void enviarAoFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;
  FirebaseJson json;
  json.set("luminosidade", valorLDR);
  json.set("pressao", pressao);
  json.set("temperatura", temperaturaBMP);
  json.set("altitude", altitude);
  json.set("chuva_mm", chuvaAcumulada);
  json.set("cliques_chuva", cliquesChuva);
  json.set("hora_ultima_chuva", horaUltimoClique);
  
  if (!isnan(umidade)) {
    json.set("umidade", umidade);
  }
  Firebase.updateNode(firebaseData, "/estacao", json);
}

void salvarNoHistorico() {
  if (WiFi.status() != WL_CONNECTED) return;
  FirebaseJson json;
  json.set("data", obterDataAtual());
  json.set("hora", obterHoraAtual());
  json.set("luminosidade", valorLDR);
  json.set("pressao", pressao);
  json.set("temperatura", temperaturaBMP);
  json.set("altitude", altitude);
  json.set("chuva_mm", chuvaAcumulada);
  
  if (!isnan(umidade)) {
    json.set("umidade", umidade); 
  }
  Firebase.push(firebaseData, "/historico", json);
}

void salvarResumoDiario() {
  if (WiFi.status() != WL_CONNECTED) return;
  FirebaseJson json;
  json.set("data", obterDataAtual());
  json.set("chuva_total", chuvaAcumulada);
  Firebase.push(firebaseData, "/resumos_diarios", json);
}

void verificarResetDaMeiaNoite() {
  time_t agora = time(nullptr);
  struct tm* timeInfo = localtime(&agora);
  int diaAtual = timeInfo->tm_mday;

  // Se mudou o dia, salva o acumulado e zera o contador
  if (diaAtual != ultimoDia && diaAtual > 0 && ultimoDia > 0) {
    salvarResumoDiario();
    cliquesChuva = 0; // Reseta os cliques da interrupção para o novo dia
    chuvaAcumulada = 0.0;
    ultimoDia = diaAtual;
    if (WiFi.status() == WL_CONNECTED) {
      enviarAoFirebase();
      salvarNoHistorico();
    }
  }
}