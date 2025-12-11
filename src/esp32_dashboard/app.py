from flask import Flask, render_template, request, jsonify
import datetime

app = Flask(__name__)

# Dicionário global para armazenar o último dado recebido de CADA dispositivo
# A chave será o ID do dispositivo (ex: "CAR_A", "ESP32_01")
latest_data = {}

@app.route('/')
def index():
    """Serve o arquivo HTML do dashboard."""
    return render_template('index.html')

@app.route('/data', methods=['POST'])
def receive_data():
    """Endpoint para o ESP32 enviar dados via HTTP POST. Agora espera um 'device_id'."""
    global latest_data
    
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
            
            # Armazena os dados
            latest_data[device_id] = {
                "rpm": data['rpm'],
                "speed": data['speed'],
                "temp_motor": data['temp_motor'],
                "throttle_pos": data['throttle_pos'],
                "timestamp": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            }
            
            print(f"Dados recebidos de {device_id}: {latest_data[device_id]}")
            return jsonify({"status": "success", "message": f"Data received from {device_id}"}), 200
        else:
            return jsonify({"status": "error", "message": f"Missing one of the required keys ({', '.join(required_keys)}) in JSON"}), 400
    else:
        return jsonify({"status": "error", "message": "Content-Type must be application/json"}), 400

@app.route('/latest', methods=['GET'])
def get_latest_data():
    """Endpoint para o frontend buscar os dados de TODOS os dispositivos."""
    # Retorna o dicionário completo de dados
    return jsonify(latest_data), 200

if __name__ == '__main__':
    # O host '0.0.0.0' é importante para que o ESP32 possa acessar o servidor
    app.run(host='0.0.0.0', port=5000, debug=True)