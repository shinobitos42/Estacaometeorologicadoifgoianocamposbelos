// =====================================================================================
// PROJETO: ESTAÇÃO METEOROLÓGICA IOT COMPLETA (ESP8266 + FIREBASE)
// =====================================================================================

// Importação das Bibliotecas necessárias
#include <ESP8266WiFi.h>      // Conexão Wi-Fi
#include <FirebaseESP8266.h>  // Conexão com Firebase (Mobizt)
#include <time.h>             // Sincronização de Data e Hora
#include <Wire.h>             // Comunicação I2C
#include <Adafruit_Sensor.h>  // Base de sensores
#include <Adafruit_BMP280.h>  // Barômetro
#include <DHT.h>              // Sensor de Umidade

// [FUTURO LCD] Descomente a linha abaixo quando for instalar a tela LCD
// #include <LiquidCrystal_I2C.h> 

// =====================================================
// 1. CONFIGURAÇÕES DA REDE WI-FI E FIREBASE
// =====================================================
#define WIFI_SSID "Loira do banheiro"
#define WIFI_PASSWORD "arrozemanueca"

#define FIREBASE_HOST "estacao-fisica-lab-if-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "KsBqqQ89BK5C5AYl2BoKSdQUNSyII2qQmyRfvBdN"

// =====================================================
// 2. PINAGEM DOS SENSORES E DISPLAY
// =====================================================
const int pinoLDR = A0;      // Sensor de Luz (LDR)

#define DHTPIN D5            // Sensor de Umidade DHT11
#define DHTTYPE DHT11        
DHT dht(DHTPIN, DHTTYPE);

const int PINO_CHUVA = D6;   // Sensor Hall do Pluviômetro (Chuva)

// [FUTURO VENTO] Descomente quando for plugar o sensor do anemômetro
// const int PINO_VENTO = D7;   

// Barômetro BMP280 (Ligado nos pinos I2C: D1 e D2)
Adafruit_BMP280 bmp; 

// [FUTURO LCD] Descomente a linha abaixo quando instalar a tela
// LiquidCrystal_I2C lcd(0x27, 16, 2); 

// =====================================================
// 3. VARIÁVEIS DOS SENSORES ROTINEIROS
// =====================================================
int valorLDR = 0;
String ultimaCondicaoSalva = ""; 

float pressao = 0.0;
float temperaturaBMP = 0.0;
float altitude = 0.0; 
float umidade = 0.0; 

// [FUTURO LCD] Variável de controle das telas
// int telaAtual = 0; 

unsigned long tempoAnteriorSensores = 0;
const unsigned long intervaloSensores = 5000; // Lê sensores a cada 5 segundos

unsigned long tempoAnteriorWiFi = 0;
const unsigned long intervaloWiFi = 5000;

// =====================================================
// 4. VARIÁVEIS DE INTERRUPÇÃO (CHUVA E VENTO)
// =====================================================
// CHUVA
volatile int cliquesChuva = 0; 
volatile unsigned long tempoUltimoChuva = 0;
volatile bool atualizarChuvaFirebase = false; 
const unsigned long cooldownChuva = 150;     // Evita pulso duplo do ímã da chuva
const float MILIMETROS_POR_BASCULA = 0.25; 
float chuvaAcumulada = 0.0;
String horaUltimoClique = "--:--:--";
int ultimoDia = 0;

// [FUTURO VENTO] Variáveis para o cálculo da velocidade do vento
// volatile int cliquesVento = 0; 
// float vento_kmh = 0.0;
// const float FATOR_VENTO = 1.5; // Calibração do cata-vento para km/h

// =====================================================
// 5. INSTÂNCIAS DO FIREBASE
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
// INTERRUPÇÕES DE HARDWARE
// =====================================================
void ICACHE_RAM_ATTR contarChuva() {
  unsigned long tempoAtual = millis();
  if (tempoAtual - tempoUltimoChuva > cooldownChuva) {
    cliquesChuva++;
    tempoUltimoChuva = tempoAtual;
    atualizarChuvaFirebase = true;
  }
}

// [FUTURO VENTO] Função disparada pelo ímã do anemômetro
// void ICACHE_RAM_ATTR contarVento() {
//   cliquesVento++;
// }

// =====================================================
// SETUP (INICIALIZAÇÃO)
// =====================================================
void setup() {
  Serial.begin(115200);
  Wire.begin(); 

  // [FUTURO LCD] Descomente as linhas abaixo para ligar a tela no setup
  // lcd.init();       
  // lcd.backlight();  
  // lcd.setCursor(0, 0); lcd.print("Estacao IoT");
  // lcd.setCursor(0, 1); lcd.print("Iniciando...");

  // Configurações das Interrupções da Chuva
  pinMode(PINO_CHUVA, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PINO_CHUVA), contarChuva, FALLING);
  Serial.println("\n[OK] Sensor de Chuva ativo no D6!");

  // [FUTURO VENTO] Configurações da Interrupção do Vento
  // pinMode(PINO_VENTO, INPUT_PULLUP);
  // attachInterrupt(digitalPinToInterrupt(PINO_VENTO), contarVento, FALLING);
  // Serial.println("[OK] Sensor de Vento ativo no D7!");

  // Inicia Sensores
  dht.begin();
  Serial.println("[OK] Sensor DHT11 iniciado!");
  
  Serial.println("Procurando Barometro BMP280...");
  if (!bmp.begin(0x76) && !bmp.begin(0x77)) {
    Serial.println("[ERRO CRITICO] BMP280 nao encontrado!");
  } else {
    Serial.println("[OK] BMP280 iniciado!");
  }

  // Conexões de Rede e Firebase
  conectarWiFi();
  sincronizarHora();
  configurarFirebase();

  // Leituras base iniciais
  valorLDR = analogRead(pinoLDR);
  ultimaCondicaoSalva = obterCategoriaLDR(valorLDR);

  time_t agora = time(nullptr);
  struct tm* timeInfo = localtime(&agora);
  ultimoDia = timeInfo->tm_mday;

  // [FUTURO LCD] Descomente para informar que conectou
  // lcd.clear();
  // lcd.setCursor(0, 0);
  // lcd.print("Conectado!");
  // delay(1000);
}

// =====================================================
// LOOP PRINCIPAL
// =====================================================
void loop() {
  verificarResetDaMeiaNoite();

  // Controle de queda de rede Wi-Fi
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

  // TRATAMENTO ASSÍNCRONO: CHUVA
  if (atualizarChuvaFirebase && WiFi.status() == WL_CONNECTED) {
    atualizarChuvaFirebase = false;
    horaUltimoClique = obterHoraAtual();
    chuvaAcumulada = cliquesChuva * MILIMETROS_POR_BASCULA;
    
    enviarAoFirebase();
    salvarNoHistorico();
  }

  // LEITURA GERAL DE SENSORES (A CADA 5 SEGUNDOS)
  if (tempoAtual - tempoAnteriorSensores >= intervaloSensores) {
    tempoAnteriorSensores = tempoAtual;
    
    // Leitura Estática
    valorLDR = analogRead(pinoLDR);
    pressao = bmp.readPressure() / 100.0F;
    temperaturaBMP = bmp.readTemperature();
    altitude = bmp.readAltitude(1013.25);
    umidade = dht.readHumidity();
    chuvaAcumulada = cliquesChuva * MILIMETROS_POR_BASCULA;

    // [FUTURO VENTO] Cálculo do Vento
    // vento_kmh = (cliquesVento / 5.0) * FATOR_VENTO; 
    // cliquesVento = 0; // Zera para ler os próximos 5 segundos

    // Serial Print (Para debugar no Computador)
    Serial.println("\n--- LEITURA ---");
    Serial.print("Luz (Bruto)  : "); Serial.println(valorLDR);
    Serial.print("Temperatura  : "); Serial.println(temperaturaBMP);
    Serial.print("Umidade      : "); Serial.println(umidade);
    Serial.print("Pressao      : "); Serial.println(pressao);
    Serial.print("Altitude     : "); Serial.println(altitude);
    Serial.print("Chuva Acumul.: "); Serial.println(chuvaAcumulada);
    
    // [FUTURO VENTO] Print do vento no monitor serial
    // Serial.print("Vento        : "); Serial.println(vento_kmh);

    // ==============================================================
    // [FUTURO LCD] ATUALIZAÇÃO DO DISPLAY LCD (Rodízio)
    // Descomente todo esse bloco abaixo quando a tela for instalada
    // ==============================================================
    /*
    lcd.clear();
    if (telaAtual == 0) {
      lcd.setCursor(0, 0); lcd.print("Temp: "); lcd.print(temperaturaBMP, 1); lcd.print("C");
      lcd.setCursor(0, 1); lcd.print("Umid: "); lcd.print(umidade, 0); lcd.print("%");
      telaAtual = 1; 
    } 
    else if (telaAtual == 1) {
      lcd.setCursor(0, 0); lcd.print("Chuva: "); lcd.print(chuvaAcumulada, 2); lcd.print("mm");
      lcd.setCursor(0, 1); lcd.print("Vento: "); lcd.print(vento_kmh, 1); lcd.print("km/h");
      telaAtual = 2;
    }
    else if (telaAtual == 2) {
      lcd.setCursor(0, 0); lcd.print("Pres: "); lcd.print(pressao, 0); lcd.print("hPa");
      lcd.setCursor(0, 1); lcd.print(obterCategoriaLDR(valorLDR).substring(0, 16));
      telaAtual = 0; 
    }
    */
    // ==============================================================

    // Envia dados completos para o Firebase (Para atualizar a aba "Painel")
    if (WiFi.status() == WL_CONNECTED) {
      enviarAoFirebase(); 
    }

    // Salva ponto no histórico apenas se o clima mudou visualmente
    String condicaoAtual = obterCategoriaLDR(valorLDR);
    if (condicaoAtual != ultimaCondicaoSalva) {
      if (WiFi.status() == WL_CONNECTED) {
        salvarNoHistorico();
        ultimaCondicaoSalva = condicaoAtual;
      }
    }
  }

  delay(10); // Pausa de estabilidade
}

// =====================================================
// FUNÇÕES AUXILIARES DE CÁLCULO E CONEXÃO
// =====================================================
String obterCategoriaLDR(int v) {
  if (v < 200) return "Muito escuro";
  if (v < 400) return "Nublado";
  if (v < 700) return "Parcialmente nublado";
  return "Plena luz solar";
}

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int t = 0; while (WiFi.status() != WL_CONNECTED && t < 30) { delay(500); t++; }
}

void sincronizarHora() {
  configTime(-3 * 3600, 0, "pool.ntp.br", "time.google.com");
  int t = 0; while (time(nullptr) < 1000000000l && t < 20) { delay(500); t++; }
}

void configurarFirebase() {
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

String obterHoraAtual() {
  time_t agora = time(nullptr); struct tm* timeInfo = localtime(&agora); char buffer[9];
  sprintf(buffer, "%02d:%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec); return String(buffer);
}

String obterDataAtual() {
  time_t agora = time(nullptr); struct tm* timeInfo = localtime(&agora); char buffer[11];
  sprintf(buffer, "%02d/%02d/%04d", timeInfo->tm_mday, timeInfo->tm_mon + 1, timeInfo->tm_year + 1900); return String(buffer);
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
  
  // [FUTURO VENTO] Descomente para enviar ao painel do site
  // json.set("vento_kmh", vento_kmh);
  
  if (!isnan(umidade)) { json.set("umidade", umidade); }
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
  
  // [FUTURO VENTO] Descomente para salvar nos relatórios do site
  // json.set("vento_kmh", vento_kmh);
  
  if (!isnan(umidade)) { json.set("umidade", umidade); }
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

  if (diaAtual != ultimoDia && diaAtual > 0 && ultimoDia > 0) {
    salvarResumoDiario();
    cliquesChuva = 0; 
    chuvaAcumulada = 0.0;
    ultimoDia = diaAtual;
    if (WiFi.status() == WL_CONNECTED) {
      enviarAoFirebase();
      salvarNoHistorico();
    }
  }
}