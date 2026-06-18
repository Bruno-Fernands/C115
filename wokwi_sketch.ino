/*
 * ============================================================
 *  Câmara de Fermentação Inteligente — ESP32 (Wokwi)
 *  Inatel C115 – Conceitos e Tecnologias para Dispositivos Conectados
 * ============================================================
 *
 * CIRCUITO (ver diagram.json):
 *   DHT22   → GPIO4  (temperatura ambiente + umidade)
 *   DS18B20 → GPIO5  (temperatura do líquido, com pullup 4.7kΩ)
 *   LED Vermelho → GPIO2  (aquecedor)
 *   LED Azul     → GPIO15 (resfriador — usado para cerveja/lager)
 *   LED Amarelo  → GPIO16 (umidificador — usado para pão)
 *   OLED SSD1306 → GPIO22 (SDA) + GPIO22 (SCL)
 *
 * MQTT: simulado no Serial Monitor.
 *   Em hardware real, usar a biblioteca PubSubClient para
 *   substituir a função publicarMQTT() pela publicação real.
 *
 * TROCA DE PERFIL: envie 1, 2, 3 ou 4 pelo Serial Monitor.
 *   1 = Pão   2 = Cerveja Ale   3 = Cerveja Lager   4 = Kombucha
 *
 * SIMULAR CONDIÇÕES: clique no DHT22 ou DS18B20 no Wokwi
 *   para alterar temperatura/umidade em tempo real.
 * ============================================================
 */

#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ─── PINOS ──────────────────────────────────────────────────────
#define PINO_DHT22         4
#define PINO_DS18B20       5
#define PINO_AQUECEDOR     2   // LED Vermelho  – aquece câmara
#define PINO_RESFRIADOR   15   // LED Azul      – resfria câmara (cerveja)
#define PINO_UMIDIFICADOR 16   // LED Amarelo   – umidifica câmara (pão)

// ─── OLED SSD1306 ───────────────────────────────────────────────
#define OLED_W   128
#define OLED_H    64
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

// ─── SENSORES ───────────────────────────────────────────────────
DHT dht(PINO_DHT22, DHT22);
OneWire barramento(PINO_DS18B20);
DallasTemperature ds18b20(&barramento);

// ─── PERFIS DE FERMENTAÇÃO ──────────────────────────────────────
//
// Fontes dos valores:
//   Pão:      Gisslen, Professional Baking (2012)
//   Ale:      Wyeast 1056 / White Labs WLP001 data sheet
//   Lager:    Wyeast 2124 / White Labs WLP830 data sheet
//   Kombucha: Katz, The Art of Fermentation (2012)
//
struct Perfil {
  const char* nome;
  float  temp_min;        // Abaixo: fermentação lenta
  float  temp_max;        // Acima: off-flavors ou estresse
  float  temp_crit_min;   // Abaixo: dormência do fermento
  float  temp_crit_max;   // Acima: MORTE do fermento
  float  umid_min;        // % RH mínima (pão: evita ressecamento)
  float  umid_max;        // % RH máxima
  bool   usa_resfriador;  // Cerveja lager precisa resfriar ativamente
  bool   usa_umidificador;// Pão precisa umidade alta
  const char* topico;     // Prefixo dos tópicos MQTT
};

const Perfil PERFIS[4] = {
  // nome       t_min t_max  t_crit_min t_crit_max  u_min u_max  resfr  umid   topico
  {"Pao",       24.0, 27.0,  10.0,      38.0,       75.0, 85.0,  false, true,  "inatel/fermentacao/pao"},
  {"Ale",       18.0, 22.0,  15.0,      26.0,       40.0, 80.0,  true,  false, "inatel/fermentacao/cerveja/ale"},
  {"Lager",      8.0, 12.0,   4.0,      15.0,       40.0, 80.0,  true,  false, "inatel/fermentacao/cerveja/lager"},
  {"Kombucha",  24.0, 29.0,  18.0,      32.0,       40.0, 80.0,  false, false, "inatel/fermentacao/kombucha"},
};

int perfilAtual = 0;  // Perfil inicial: Pão

// ─── ESTADO DOS ATUADORES ───────────────────────────────────────
bool aquecedorLigado    = false;
bool resfriadorLigado   = false;
bool umidificadorLigado = false;

unsigned long ultimaLeitura = 0;
const unsigned long INTERVALO = 2000; // 2 s na simulação = ~30 s no hardware real

// ─── MQTT SIMULADO ──────────────────────────────────────────────

void mqttPublish(const char* subtopico, float valor, const char* unidade) {
  char topico[100];
  snprintf(topico, sizeof(topico), "%s/%s", PERFIS[perfilAtual].topico, subtopico);
  Serial.print("[MQTT] PUB  ");
  Serial.print(topico);
  Serial.print("  {\"valor\":");
  Serial.print(valor, 2);
  Serial.print(", \"unidade\":\"");
  Serial.print(unidade);
  Serial.println("\"}");
}

void mqttControle(const char* atuador, bool ligado) {
  char topico[100];
  snprintf(topico, sizeof(topico), "%s/controle/%s", PERFIS[perfilAtual].topico, atuador);
  Serial.print("[MQTT] PUB  ");
  Serial.print(topico);
  Serial.print("  {\"estado\":\"");
  Serial.print(ligado ? "ligado" : "desligado");
  Serial.println("\"}");
}

void mqttAlerta(const char* tipo, float valor, const char* msg) {
  char topico[100];
  snprintf(topico, sizeof(topico), "%s/alerta", PERFIS[perfilAtual].topico);
  Serial.print("[MQTT] ⚠ ALERTA  ");
  Serial.print(topico);
  Serial.print("  {\"tipo\":\"");
  Serial.print(tipo);
  Serial.print("\", \"valor\":");
  Serial.print(valor, 2);
  Serial.print(", \"msg\":\"");
  Serial.print(msg);
  Serial.println("\"}");
}

// ─── CONTROLE BANG-BANG COM HISTERESE ───────────────────────────
//
// Histerese de 0.5°C evita que o atuador ligue e desligue
// repetidamente quando a temperatura está na borda da faixa.

void controlarAtuadores(float temp, float umid) {
  const Perfil& p = PERFIS[perfilAtual];

  // --- Aquecedor ---
  if (temp < p.temp_min - 0.5f && !aquecedorLigado) {
    aquecedorLigado = true;
    digitalWrite(PINO_AQUECEDOR, HIGH);
    mqttControle("aquecedor", true);
    Serial.print("  >> Aquecedor LIGADO (temp=");
    Serial.print(temp, 1); Serial.print(" < "); Serial.print(p.temp_min); Serial.println("C)");
  }
  else if (temp > p.temp_max + 0.5f && aquecedorLigado) {
    aquecedorLigado = false;
    digitalWrite(PINO_AQUECEDOR, LOW);
    mqttControle("aquecedor", false);
    Serial.print("  >> Aquecedor DESLIGADO (temp=");
    Serial.print(temp, 1); Serial.print(" > "); Serial.print(p.temp_max); Serial.println("C)");
  }

  // --- Resfriador (apenas cerveja) ---
  if (p.usa_resfriador) {
    if (temp > p.temp_max + 0.5f && !resfriadorLigado) {
      resfriadorLigado = true;
      digitalWrite(PINO_RESFRIADOR, HIGH);
      mqttControle("resfriador", true);
      Serial.println("  >> Resfriador LIGADO");
    }
    else if (temp < p.temp_min - 0.5f && resfriadorLigado) {
      resfriadorLigado = false;
      digitalWrite(PINO_RESFRIADOR, LOW);
      mqttControle("resfriador", false);
      Serial.println("  >> Resfriador DESLIGADO");
    }
  }

  // --- Umidificador (pão) ---
  if (p.usa_umidificador) {
    if (umid < p.umid_min - 2.0f && !umidificadorLigado) {
      umidificadorLigado = true;
      digitalWrite(PINO_UMIDIFICADOR, HIGH);
      mqttControle("umidificador", true);
      Serial.println("  >> Umidificador LIGADO");
    }
    else if (umid > p.umid_max + 2.0f && umidificadorLigado) {
      umidificadorLigado = false;
      digitalWrite(PINO_UMIDIFICADOR, LOW);
      mqttControle("umidificador", false);
      Serial.println("  >> Umidificador DESLIGADO");
    }
  }

  // --- Alertas críticos ---
  if (temp < p.temp_crit_min) {
    mqttAlerta("temp_crit_baixa", temp, "Risco de dormencia do fermento!");
  }
  if (temp > p.temp_crit_max) {
    mqttAlerta("temp_crit_alta", temp, "MORTE DO FERMENTO - intervencao necessaria!");
  }
}

// ─── OLED ───────────────────────────────────────────────────────

void atualizarOLED(float tempLiq, float tempAmb, float umid) {
  const Perfil& p = PERFIS[perfilAtual];
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  // Cabeçalho
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Perfil: "); oled.println(p.nome);
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Temperatura do líquido em destaque
  oled.setTextSize(2);
  oled.setCursor(0, 12);
  oled.print(tempLiq, 1);
  oled.print((char)247); // símbolo de grau
  oled.println("C");

  // Dados secundários
  oled.setTextSize(1);
  oled.setCursor(0, 32);
  oled.print("Amb:");
  oled.print(tempAmb, 1);
  oled.print("C  Um:");
  oled.print((int)umid); oled.println("%");

  oled.setCursor(0, 42);
  oled.print("Alvo: ");
  oled.print(p.temp_min, 0); oled.print("-"); oled.print(p.temp_max, 0); oled.println("C");

  // Status dos atuadores (LEDs)
  oled.setCursor(0, 53);
  oled.print(aquecedorLigado    ? "[AQUEC] " : "        ");
  oled.print(resfriadorLigado   ? "[RESFR] " : "        ");
  oled.print(umidificadorLigado ? "[UMID]"  : "      ");

  oled.display();
}

// ─── SETUP ──────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("================================================");
  Serial.println(" Camara de Fermentacao Inteligente — Wokwi/ESP32");
  Serial.println(" Inatel C115");
  Serial.println("================================================");
  Serial.println();
  Serial.println("Comandos (enviar pelo Serial Monitor):");
  Serial.println("  1 = Pao  |  2 = Ale  |  3 = Lager  |  4 = Kombucha");
  Serial.println();

  // Pinos de saída
  pinMode(PINO_AQUECEDOR,    OUTPUT);
  pinMode(PINO_RESFRIADOR,   OUTPUT);
  pinMode(PINO_UMIDIFICADOR, OUTPUT);
  digitalWrite(PINO_AQUECEDOR,    LOW);
  digitalWrite(PINO_RESFRIADOR,   LOW);
  digitalWrite(PINO_UMIDIFICADOR, LOW);

  // Sensores
  dht.begin();
  ds18b20.begin();

  // OLED
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("ERRO: OLED SSD1306 nao encontrado!");
  } else {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(20, 20);
    oled.println("Iniciando...");
    oled.display();
  }

  // Simulação de conexão MQTT
  delay(300);
  Serial.println("[WiFi] Conectando...");
  delay(400);
  Serial.println("[WiFi] Conectado! IP: 192.168.1.100");
  Serial.println("[MQTT] Conectando ao broker test.mosquitto.org:1883...");
  delay(300);
  Serial.println("[MQTT] Conectado ao broker!");
  Serial.println("[MQTT] Publicando a cada 2 segundos...");
  Serial.println("------------------------------------------------");

  Serial.print("\nPerfil ativo: ");
  Serial.println(PERFIS[perfilAtual].nome);
}

// ─── LOOP ───────────────────────────────────────────────────────

void loop() {

  // Trocar perfil via Serial Monitor
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd >= '1' && cmd <= '4') {
      perfilAtual = cmd - '1';
      // Desligar tudo ao trocar perfil
      aquecedorLigado = resfriadorLigado = umidificadorLigado = false;
      digitalWrite(PINO_AQUECEDOR,    LOW);
      digitalWrite(PINO_RESFRIADOR,   LOW);
      digitalWrite(PINO_UMIDIFICADOR, LOW);
      Serial.println();
      Serial.print(">>> PERFIL TROCADO: ");
      Serial.println(PERFIS[perfilAtual].nome);
      Serial.print(">>> Alvo: ");
      Serial.print(PERFIS[perfilAtual].temp_min);
      Serial.print(" - ");
      Serial.print(PERFIS[perfilAtual].temp_max);
      Serial.println(" C");
      Serial.println("------------------------------------------------");
    }
  }

  // Aguardar intervalo
  unsigned long agora = millis();
  if (agora - ultimaLeitura < INTERVALO) return;
  ultimaLeitura = agora;

  // ── Leitura dos sensores ──
  float tempAmb = dht.readTemperature();
  float umid    = dht.readHumidity();
  ds18b20.requestTemperatures();
  float tempLiq = ds18b20.getTempCByIndex(0);

  // Validação DHT22
  if (isnan(tempAmb) || isnan(umid)) {
    Serial.println("[ERRO] Falha na leitura do DHT22. Verificar conexao.");
    return;
  }

  // Validação DS18B20
  if (tempLiq == DEVICE_DISCONNECTED_C) {
    Serial.println("[AVISO] DS18B20 desconectado. Usando temp. ambiente.");
    tempLiq = tempAmb;
  }

  // ── Publicar via MQTT (simulado) ──
  mqttPublish("sensor/temperatura_liquido",  tempLiq, "C");
  mqttPublish("sensor/temperatura_ambiente", tempAmb, "C");
  mqttPublish("sensor/umidade",              umid,    "%");

  // ── Controle dos atuadores ──
  controlarAtuadores(tempLiq, umid);

  // ── Atualizar OLED ──
  atualizarOLED(tempLiq, tempAmb, umid);

  // ── Log resumido no Serial ──
  Serial.print("[SENSOR] Liq:");
  Serial.print(tempLiq, 1);
  Serial.print("C  Amb:");
  Serial.print(tempAmb, 1);
  Serial.print("C  Umid:");
  Serial.print((int)umid);
  Serial.print("%  [");
  Serial.print(PERFIS[perfilAtual].nome);
  Serial.print(" - Alvo:");
  Serial.print(PERFIS[perfilAtual].temp_min, 0);
  Serial.print("-");
  Serial.print(PERFIS[perfilAtual].temp_max, 0);
  Serial.println("C]");
}
