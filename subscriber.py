import json
from datetime import datetime

import paho.mqtt.client as mqtt

BROKER    = "test.mosquitto.org"
PORT      = 1883
CLIENT_ID = "fermentacao_app_monitor"

TOPICO_TODOS = "inatel/fermentacao/#"


def formatar(topico: str, dados: dict) -> str:
    """Formata a mensagem recebida de forma legível no terminal."""
    agora = datetime.now().strftime("%H:%M:%S")

    if "sensor/temperatura" in topico:
        v = dados.get("valor", "?")
        return f"[{agora}] 🌡  TEMPERATURA:    {v} °C"

    elif "sensor/umidade" in topico:
        v = dados.get("valor", "?")
        return f"[{agora}] 💧 UMIDADE:         {v} %"

    elif "sensor/co2" in topico:
        v = dados.get("valor", "?")
        return f"[{agora}] 💨 CO₂:             {v} ppm"

    elif "controle/aquecedor" in topico:
        estado = dados.get("estado", "?").upper()
        simbolo = "🔴" if estado == "LIGADO" else "⚪"
        motivo  = dados.get("motivo", "")
        return f"[{agora}] {simbolo} AQUECEDOR:     {estado}  ({motivo})"

    elif "controle/umidificador" in topico:
        estado = dados.get("estado", "?").upper()
        simbolo = "🔵" if estado == "LIGADO" else "⚪"
        return f"[{agora}] {simbolo} UMIDIFICADOR:  {estado}"

    elif "alerta" in topico:
        msg = dados.get("mensagem", str(dados))
        return f"[{agora}] ⚠️  ALERTA:  {msg}"

    else:
        return f"[{agora}] 📨 {topico}: {json.dumps(dados, ensure_ascii=False)}"

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Conectado ao broker {BROKER}:{PORT}")
        client.subscribe(TOPICO_TODOS)
        print(f"[MQTT] Assinando: {TOPICO_TODOS}\n")
        print("=" * 55)
        print("  App de Monitoramento — aguardando dados...")
        print("  (Execute publisher.py em outro terminal)")
        print("=" * 55 + "\n")
    else:
        print(f"[MQTT] Falha na conexão — código: {rc}")


def on_message(client, userdata, message):
    """Callback chamado a cada mensagem recebida no tópico assinado."""
    try:
        payload_str = message.payload.decode("utf-8")
        dados = json.loads(payload_str)
        print(formatar(message.topic, dados))
    except json.JSONDecodeError:
        agora = datetime.now().strftime("%H:%M:%S")
        print(f"[{agora}] {message.topic}: {message.payload.decode('utf-8')}")


def on_disconnect(client, userdata, rc):
    if rc != 0:
        print(f"\n[MQTT] Desconectado inesperadamente — código: {rc}")


def main():
    print("=" * 55)
    print("  Câmara de Fermentação — Monitor (Subscriber)")
    print("  Inatel C115")
    print("=" * 55)
    print(f"[CONFIG] Broker: {BROKER}:{PORT}")
    print("[CONFIG] Pressione Ctrl+C para encerrar.\n")

    client = mqtt.Client(client_id=CLIENT_ID)
    client.on_connect    = on_connect
    client.on_message    = on_message
    client.on_disconnect = on_disconnect

    client.connect(BROKER, PORT, keepalive=60)

    try:
        client.loop_forever()
    except KeyboardInterrupt:
        print("\n[APP] Monitor encerrado pelo usuário.")
    finally:
        client.disconnect()
        print("[MQTT] Desconectado.")


if __name__ == "__main__":
    main()
