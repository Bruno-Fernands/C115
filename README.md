# Câmara de Fermentação Inteligente — IoT com MQTT
**C115 · Conceitos e Tecnologias para Dispositivos Conectados · Inatel · 2026**

---

## Estrutura do repositório

```
fermentacao-iot/
├── apresentacao/
│   └── Fermentacao_IoT.pptx
├── simulacao/
│   ├── publisher.py       # Simula o ESP32 publicando dados
│   ├── subscriber.py      # Simula o app móvel recebendo dados
│   └── requirements.txt
└── README.md
```

---

## Pré-requisitos

- Python 3.8 ou superior
- Conexão com a internet (o broker é público)

---

## Instalação

```bash
pip install -r requirements.txt
```

---

## Como executar a simulação

Abra **dois terminais separados**.

**Terminal 1 — Publisher (simula o ESP32):**
```bash
python publisher.py
```
Escolha o perfil: `1` Pão · `2` Cerveja Ale · `3` Kombucha

**Terminal 2 — Subscriber (simula o app móvel):**
```bash
python subscriber.py
```

O subscriber exibirá em tempo real tudo que o publisher publicar.

---

## Broker MQTT utilizado

| Parâmetro | Valor |
|---|---|
| Host | `test.mosquitto.org` |
| Porta | `1883` |
| Autenticação | Nenhuma (público) |

---

## Tópicos MQTT

| Tópico | Direção | QoS | Conteúdo |
|---|---|---|---|
| `inatel/fermentacao/sensor/temperatura` | ESP32 → Broker | 0 | `{"valor": 25.3, "unidade": "°C"}` |
| `inatel/fermentacao/sensor/umidade` | ESP32 → Broker | 0 | `{"valor": 78.2, "unidade": "%"}` |
| `inatel/fermentacao/sensor/co2` | ESP32 → Broker | 0 | `{"valor": 1250, "unidade": "ppm"}` |
| `inatel/fermentacao/controle/aquecedor` | Broker ↔ ESP32 | 1 | `{"estado": "ligado"}` |
| `inatel/fermentacao/controle/umidificador` | Broker ↔ ESP32 | 1 | `{"estado": "desligado"}` |
| `inatel/fermentacao/alerta` | ESP32 → Broker | 1 | `{"tipo": "...", "mensagem": "..."}` |

---

## Usando o MQTT Explorer (opcional)

1. Baixar em: **mqtt-explorer.com**
2. Nova conexão → Host: `test.mosquitto.org` · Porta: `1883`
3. Clicar em **Connect**
4. Buscar: `inatel/fermentacao/#`
5. Visualizar gráficos e histórico em tempo real

---

## Usando o IoT MQTT Panel (mobile)

1. Instalar da Play Store / App Store
2. Novo broker: `test.mosquitto.org` · Porta: `1883`
3. Criar widgets:
   - **Gauge** → `inatel/fermentacao/sensor/temperatura`
   - **Gauge** → `inatel/fermentacao/sensor/umidade`
   - **Text log** → `inatel/fermentacao/alerta`
   - **Switch** → `inatel/fermentacao/controle/aquecedor`

---

## Valores científicos utilizados

| Parâmetro | Pão | Cerveja Ale | Kombucha |
|---|---|---|---|
| Temp. ideal | 24–27°C | 18–22°C | 24–29°C |
| Temp. letal | >38°C | >38°C | >35°C |
| Umidade ideal | 75–85% | — | — |
| CO₂ alerta | >2.000 ppm | >3.000 ppm | >2.500 ppm |

**Fontes:** Wyeast/White Labs strain data sheets; Katz, *The Art of Fermentation* (2012); Gisslen, *Professional Baking* (2012)
