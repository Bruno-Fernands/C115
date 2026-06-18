import json
import math
import random
import time
from datetime import datetime

import paho.mqtt.client as mqtt

BROKER    = "test.mosquitto.org"
PORT      = 1883
CLIENT_ID = "fermentacao_esp32_simulado"

TOPIC_TEMP    = "inatel/fermentacao/sensor/temperatura"
TOPIC_UMID    = "inatel/fermentacao/sensor/umidade"
TOPIC_CO2     = "inatel/fermentacao/sensor/co2"
TOPIC_AQUEC   = "inatel/fermentacao/controle/aquecedor"
TOPIC_UMIDIF  = "inatel/fermentacao/controle/umidificador"
TOPIC_ALERTA  = "inatel/fermentacao/alerta"


PERFIS = {
    "1": {
        "nome":            "Pão Artesanal",
        "temp_alvo":       25.0,   # Ótimo: 24–27°C
        "temp_min":        24.0,   # Abaixo: fermentação lenta
        "temp_max":        27.0,   # Acima: risco de estresse
        "temp_critica_min": 10.0,  # Dormência do fermento
        "temp_critica_max": 38.0,  # Morte do fermento
        "umidade_min":     75.0,   # Abaixo: superfície da massa resseca
        "umidade_max":     85.0,   # 75–85% RH é a faixa ideal
        "co2_alerta":    2000.0,   # Câmara pequena: ventilar acima disso
    },
    "2": {
        "nome":            "Cerveja Artesanal",
        "temp_alvo":       20.0,   # Ótimo: 18–22°C
        "temp_min":        18.0,
        "temp_max":        22.0,
        "temp_critica_min": 15.0,  # Fermentação muito lenta
        "temp_critica_max": 26.0,  # Produção de fusel alcohols e ésteres
        "umidade_min":     40.0,
        "umidade_max":     80.0,
        "co2_alerta":    3000.0,
    },
    "3": {
        "nome":            "Kombucha",
        "temp_alvo":       26.0,   # Ótimo: 24–29°C
        "temp_min":        24.0,
        "temp_max":        29.0,
        "temp_critica_min": 18.0,  # Fermentação muito lenta
        "temp_critica_max": 32.0,  # Favorece bactérias indesejadas
        "umidade_min":     40.0,
        "umidade_max":     80.0,
        "co2_alerta":    2500.0,
    },
}


def simular_temperatura(temp_atual, perfil, aquecedor_ligado):
    """
    Simula a variação de temperatura da câmara.
    Com aquecedor ligado, sobe gradualmente.
    Sem aquecedor, cai (perda de calor para o ambiente).
    """
    if aquecedor_ligado:
        delta = random.uniform(0.1, 0.5)
    else:
        delta = random.uniform(-0.4, 0.1)
    return round(temp_atual + delta, 2)


def simular_umidade(umidade_atual, perfil, umidificador_ligado):
    """
    Simula variação de umidade relativa.
    Umidificador ligado aumenta; desligado cai lentamente.
    """
    if umidificador_ligado:
        delta = random.uniform(0.2, 0.8)
    else:
        delta = random.uniform(-0.5, 0.1)
    return round(max(0.0, min(100.0, umidade_atual + delta)), 2)


def simular_co2(minutos_decorridos, perfil):
    """
    Simula CO₂ com curva logística realista.
    O fermento leva tempo para ativar, depois aumenta a produção de CO₂.
    400 ppm é o nível ambiente normal.
    """
    co2_base = 400.0
    co2_pico = perfil["co2_alerta"] * 0.85

    k      = 0.06
    t_meio = 25.0
    atividade = co2_pico / (1 + math.exp(-k * (minutos_decorridos - t_meio)))

    ruido = random.uniform(-30, 30)
    return round(co2_base + atividade + ruido, 0)


def payload(valor, unidade, perfil_nome):
    """Monta o payload JSON padronizado para publicação."""
    return json.dumps({
        "valor":     valor,
        "unidade":   unidade,
        "perfil":    perfil_nome,
        "timestamp": datetime.now().isoformat(),
    })


def controlar_aquecedor(client, temp, perfil, estado_atual):
    """Liga aquecedor se temp < min-0.5°C; desliga se temp > max+0.5°C."""
    novo = estado_atual
    if temp < perfil["temp_min"] - 0.5 and not estado_atual:
        novo = True
        msg = json.dumps({"estado": "ligado", "motivo": f"temp {temp}°C abaixo de {perfil['temp_min']}°C"})
        client.publish(TOPIC_AQUEC, msg, qos=1)
        print(f"  [ATUADOR] 🔴 Aquecedor LIGADO  (temp={temp}°C)")
    elif temp > perfil["temp_max"] + 0.5 and estado_atual:
        novo = False
        msg = json.dumps({"estado": "desligado", "motivo": f"temp {temp}°C acima de {perfil['temp_max']}°C"})
        client.publish(TOPIC_AQUEC, msg, qos=1)
        print(f"  [ATUADOR] ⚪ Aquecedor DESLIGADO (temp={temp}°C)")
    return novo


def controlar_umidificador(client, umidade, perfil, estado_atual):
    """Liga umidificador se umidade < min-2%; desliga se > max+2%."""
    novo = estado_atual
    if umidade < perfil["umidade_min"] - 2 and not estado_atual:
        novo = True
        client.publish(TOPIC_UMIDIF, json.dumps({"estado": "ligado"}), qos=1)
        print(f"  [ATUADOR] 🔵 Umidificador LIGADO  (umidade={umidade}%)")
    elif umidade > perfil["umidade_max"] + 2 and estado_atual:
        novo = False
        client.publish(TOPIC_UMIDIF, json.dumps({"estado": "desligado"}), qos=1)
        print(f"  [ATUADOR] ⚪ Umidificador DESLIGADO (umidade={umidade}%)")
    return novo


def verificar_alertas(client, temp, co2, perfil):
    """Publica alertas críticos com QoS 1 (garantia de entrega)."""
    if temp < perfil["temp_critica_min"]:
        alerta = {
            "tipo":     "temperatura_critica_baixa",
            "valor":    temp,
            "mensagem": f"CRÍTICO: {temp}°C — risco de dormência do fermento! Mínimo: {perfil['temp_critica_min']}°C",
        }
        client.publish(TOPIC_ALERTA, json.dumps(alerta), qos=1)
        print(f"  [ALERTA] ⚠  {alerta['mensagem']}")

    if temp > perfil["temp_critica_max"]:
        alerta = {
            "tipo":     "temperatura_critica_alta",
            "valor":    temp,
            "mensagem": f"CRÍTICO: {temp}°C — risco de MORTE do fermento! Máximo: {perfil['temp_critica_max']}°C",
        }
        client.publish(TOPIC_ALERTA, json.dumps(alerta), qos=1)
        print(f"  [ALERTA] ⚠  {alerta['mensagem']}")

    if co2 > perfil["co2_alerta"]:
        alerta = {
            "tipo":     "co2_elevado",
            "valor":    co2,
            "mensagem": f"AVISO: CO₂ = {co2:.0f} ppm (limite: {perfil['co2_alerta']:.0f} ppm). Considere ventilar.",
        }
        client.publish(TOPIC_ALERTA, json.dumps(alerta), qos=1)
        print(f"  [ALERTA] ⚠  {alerta['mensagem']}")


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Conectado ao broker {BROKER}:{PORT}")
        client.subscribe("inatel/fermentacao/controle/#")
    else:
        print(f"[MQTT] Falha na conexão — código: {rc}")


def on_message(client, userdata, message):
    """Recebe comandos vindos do app móvel ou dashboard."""
    try:
        dados = json.loads(message.payload.decode("utf-8"))
        print(f"[COMANDO] {message.topic}: {dados}")
    except Exception:
        pass


def main():
    print("=" * 58)
    print("  Câmara de Fermentação Inteligente — Simulador IoT")
    print("  Inatel C115")
    print("=" * 58)
    print("\nEscolha o perfil de fermentação:")
    print("  1. Pão Artesanal")
    print("  2. Cerveja Artesanal Ale")
    print("  3. Kombucha")

    opcao = input("\nDigite 1, 2 ou 3: ").strip()
    if opcao not in PERFIS:
        print("Opção inválida. Usando perfil de Pão.")
        opcao = "1"

    perfil = PERFIS[opcao]
    print(f"\n[CONFIG] Perfil: {perfil['nome']}")
    print(f"[CONFIG] Temperatura alvo: {perfil['temp_alvo']}°C  (faixa: {perfil['temp_min']}–{perfil['temp_max']}°C)")
    print(f"[CONFIG] Alerta CO₂ acima de: {perfil['co2_alerta']:.0f} ppm")
    print(f"[CONFIG] Broker: {BROKER}:{PORT}\n")

    client = mqtt.Client(client_id=CLIENT_ID)
    client.on_connect = on_connect
    client.on_message = on_message

    print("[MQTT] Conectando...")
    client.connect(BROKER, PORT, keepalive=60)
    client.loop_start()
    time.sleep(1.5)

    temp        = 21.0
    umidade     = 60.0
    aquecedor   = False
    umidificador = False
    inicio      = time.time()

    print("[SIM] Iniciando. Pressione Ctrl+C para parar.\n")
    print(f"{'Tempo':>7} | {'Temp(°C)':>9} | {'Umid(%)':>8} | {'CO₂(ppm)':>9} | {'Aquec.':>8} | {'Umidif.':>8}")
    print("-" * 70)

    try:
        while True:
            minutos = (time.time() - inicio) / 60.0

            temp        = simular_temperatura(temp, perfil, aquecedor)
            umidade     = simular_umidade(umidade, perfil, umidificador)
            co2         = simular_co2(minutos, perfil)

            client.publish(TOPIC_TEMP,  payload(temp,    "°C",  perfil["nome"]), qos=0)
            client.publish(TOPIC_UMID,  payload(umidade, "%",   perfil["nome"]), qos=0)
            client.publish(TOPIC_CO2,   payload(co2,     "ppm", perfil["nome"]), qos=0)

            aquecedor    = controlar_aquecedor(client, temp, perfil, aquecedor)
            umidificador = controlar_umidificador(client, umidade, perfil, umidificador)

            verificar_alertas(client, temp, co2, perfil)

            aq = "LIGADO" if aquecedor    else "deslig."
            um = "LIGADO" if umidificador else "deslig."
            print(f"{minutos:>6.1f}m | {temp:>9.2f} | {umidade:>8.2f} | {co2:>9.0f} | {aq:>8} | {um:>8}")

            time.sleep(2)

    except KeyboardInterrupt:
        print("\n[SIM] Encerrado pelo usuário.")
    finally:
        client.loop_stop()
        client.disconnect()
        print("[MQTT] Desconectado.")


if __name__ == "__main__":
    main()
