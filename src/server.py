from flask import Flask, render_template, request, jsonify
import datetime
from typing import Dict, Any
from threading import Timer
import time

app = Flask(__name__)

# Dicionário global para armazenar o último dado recebido de CADA dispositivo
latest_data: Dict[str, Dict[str, Any]] = {}

# Dicionário para armazenar o último timestamp de cada dispositivo
last_seen: Dict[str, float] = {}

# Tempo máximo sem comunicação para considerar desconectado (em segundos)
CONNECTION_TIMEOUT = 15  # 15 segundos

def check_connection_status():
    """Função periódica para verificar status de conexão dos dispositivos"""
    current_time = time.time()
    for device_id in list(latest_data.keys()):
        if device_id in last_seen:
            time_since_last_seen = current_time - last_seen[device_id]
            
            # Adiciona status de conexão aos dados
            if device_id in latest_data:
                latest_data[device_id]['connection_status'] = 'connected' if time_since_last_seen < CONNECTION_TIMEOUT else 'disconnected'
                latest_data[device_id]['last_seen_seconds'] = round(time_since_last_seen, 1)
                
                # Se desconectado há muito tempo, marca como inativo
                if time_since_last_seen > CONNECTION_TIMEOUT * 4:  # 4x o timeout
                    if 'was_connected' not in latest_data[device_id]:
                        latest_data[device_id]['was_connected'] = True
    
    # Agenda próxima verificação
    Timer(1.0, check_connection_status).start()

@app.route('/')
def index():
    """Serve o arquivo HTML do dashboard."""
    return render_template('index.html')

@app.route('/data', methods=['POST'])
def receive_data():
    """Endpoint para o ESP32 enviar dados via HTTP POST."""
    global latest_data, last_seen
    
    # Verifica se o conteúdo é JSON
    if request.is_json:
        data = request.get_json()
        
        # Validação e extração do ID do dispositivo
        device_id = data.get('device_id')
        if not device_id:
            return jsonify({"status": "error", "message": "Missing 'device_id' in JSON"}), 400
        
        # Validação básica dos novos dados da ECU
        required_keys = ['rpm', 'speed', 'temp_motor', 'throttle_pos']
        if all(key in data for key in required_keys):
            
            # Atualiza o timestamp do último contato
            current_time = time.time()
            last_seen[device_id] = current_time
            
            # Cria um dicionário com TODOS os dados recebidos
            device_data = {
                "rpm": data['rpm'],
                "speed": data['speed'],
                "temp_motor": data['temp_motor'],
                "throttle_pos": data['throttle_pos'],
                "timestamp": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "connection_status": "connected",
                "last_seen_seconds": 0,
                "was_connected": True
            }
            
            # Adiciona TODOS os outros campos do JSON (exceto device_id)
            for key, value in data.items():
                if key != 'device_id' and key not in required_keys:
                    device_data[key] = value
            
            # Garante campos padrão para os novos campos se não existirem
            default_fields = {
                'voltage': 13.5,
                'gear': 0,
                'fuel_level': 50.0
            }
            
            for field, default_value in default_fields.items():
                if field not in device_data:
                    device_data[field] = default_value
            
            # Armazena os dados
            latest_data[device_id] = device_data
            
            print(f"Dados recebidos de {device_id}: {device_data}")
            return jsonify({"status": "success", "message": f"Data received from {device_id}"}), 200
        else:
            return jsonify({"status": "error", "message": f"Missing one of the required keys ({', '.join(required_keys)}) in JSON"}), 400
    else:
        return jsonify({"status": "error", "message": "Content-Type must be application/json"}), 400

@app.route('/latest', methods=['GET'])
def get_latest_data():
    """Endpoint para o frontend buscar os dados de TODOS os dispositivos."""
    current_time = time.time()
    
    # Atualiza status de conexão para todos os dispositivos
    for device_id, data in latest_data.items():
        if device_id in last_seen:
            time_since_last_seen = current_time - last_seen[device_id]
            data['last_seen_seconds'] = round(time_since_last_seen, 1)
            data['connection_status'] = 'connected' if time_since_last_seen < CONNECTION_TIMEOUT else 'disconnected'
        
        # Garante que todos os campos existam
        for field, default_value in [('voltage', 0.0), ('gear', 0), ('fuel_level', 0.0)]:
            if field not in data:
                data[field] = default_value
    
    # Retorna o dicionário completo de dados
    return jsonify(latest_data), 200

@app.route('/status', methods=['GET'])
def get_status():
    """Retorna apenas o status de conexão de todos os dispositivos."""
    status_report = {}
    current_time = time.time()
    
    for device_id, data in latest_data.items():
        if device_id in last_seen:
            time_since_last_seen = current_time - last_seen[device_id]
            status_report[device_id] = {
                'connected': time_since_last_seen < CONNECTION_TIMEOUT,
                'last_seen': round(time_since_last_seen, 1),
                'last_update': data.get('timestamp', 'N/A')
            }
        else:
            status_report[device_id] = {
                'connected': False,
                'last_seen': None,
                'last_update': data.get('timestamp', 'N/A')
            }
    
    return jsonify(status_report), 200

if __name__ == '__main__':
    # Inicia a verificação periódica de status
    Timer(1.0, check_connection_status).start()
    
    # O host '0.0.0.0' é importante para que o ESP32 possa acessar o servidor
    print("Servidor iniciado. Monitorando conexões com timeout de 15 segundos.")
    app.run(host='0.0.0.0', port=5000, debug=True)