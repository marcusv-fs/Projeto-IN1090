"""
Simulador de Múltiplos Dispositivos ESP32 para Telemetria Automotiva

Este script simula N dispositivos ESP32 enviando dados OBD-II para o servidor Flask.
Cada dispositivo é executado em uma thread separada e envia dados em intervalos regulares.

Uso:
    python esp32_simulator.py --devices 3 --interval 2 --server http://localhost:5000/data

Autor: Assistente ESP32
Versão: 1.0
"""

import requests
import json
import random
import time
import threading
import argparse
from datetime import datetime
from typing import Dict, List, Optional
import logging
from dataclasses import dataclass, asdict
from enum import Enum

# Configuração de logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('esp32_simulator.log'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)

class VehicleType(Enum):
    """Tipos de veículos simulados"""
    SEDAN = "Sedan"
    SUV = "SUV"
    PICKUP = "Pickup"
    HATCH = "Hatch"
    SPORT = "Esportivo"

@dataclass
class ECUData:
    """Estrutura de dados da ECU"""
    device_id: str
    rpm: int
    speed: float
    temp_motor: float
    throttle_pos: float
    voltage: float
    gear: int
    fuel_level: float
    timestamp: str
    
    def to_dict(self) -> dict:
        """Converte para dicionário"""
        return asdict(self)

class ESP32Simulator:
    """Simula um dispositivo ESP32 enviando dados de telemetria"""
    
    def __init__(self, device_id: str, server_url: str, vehicle_type: VehicleType = VehicleType.SEDAN):
        """
        Inicializa o simulador de dispositivo
        
        Args:
            device_id: ID único do dispositivo
            server_url: URL do servidor Flask
            vehicle_type: Tipo de veículo a ser simulado
        """
        self.device_id = device_id
        self.server_url = server_url
        self.vehicle_type = vehicle_type
        self.running = False
        self.thread = None
        
        # Configurações baseadas no tipo de veículo
        self.config = self._get_vehicle_config(vehicle_type)
        
        # Estado atual do dispositivo
        self.state = {
            'rpm_base': random.uniform(self.config['rpm_range'][0], self.config['rpm_range'][1]),
            'speed_base': random.uniform(self.config['speed_range'][0], self.config['speed_range'][1]),
            'temp_base': random.uniform(self.config['temp_range'][0], self.config['temp_range'][1]),
            'throttle_base': random.uniform(10, 40),
            'fuel_level': random.uniform(30, 100),
            'voltage_base': random.uniform(12.5, 13.5),
            'gear': 3,
            'last_send_time': time.time()
        }
        
        logger.info(f"Dispositivo {device_id} inicializado ({vehicle_type.value})")
    
    def _get_vehicle_config(self, vehicle_type: VehicleType) -> dict:
        """Retorna configurações específicas para cada tipo de veículo"""
        configs = {
            VehicleType.SEDAN: {
                'rpm_range': (800, 3500),
                'speed_range': (30, 120),
                'temp_range': (85, 95),
                'max_gear': 6
            },
            VehicleType.SUV: {
                'rpm_range': (900, 3200),
                'speed_range': (20, 100),
                'temp_range': (88, 98),
                'max_gear': 6
            },
            VehicleType.PICKUP: {
                'rpm_range': (1000, 3800),
                'speed_range': (25, 110),
                'temp_range': (90, 105),
                'max_gear': 5
            },
            VehicleType.HATCH: {
                'rpm_range': (850, 4500),
                'speed_range': (35, 140),
                'temp_range': (82, 92),
                'max_gear': 5
            },
            VehicleType.SPORT: {
                'rpm_range': (1200, 7000),
                'speed_range': (40, 180),
                'temp_range': (90, 110),
                'max_gear': 7
            }
        }
        return configs.get(vehicle_type, configs[VehicleType.SEDAN])
    
    def generate_realistic_data(self) -> ECUData:
        """
        Gera dados de telemetria realistas com correlação entre os parâmetros
        
        Retorna:
            Objeto ECUData com dados simulados
        """
        current_time = time.time()
        time_factor = (current_time % 300) / 300  # Ciclo de 5 minutos
        
        # Simula padrão de direção (aceleração, velocidade constante, desaceleração)
        driving_pattern = (1 + 0.5 * (1 + time_factor)) % 1.0
        
        # 1. RPM correlacionado com throttle e velocidade
        throttle_variation = 0.5 + 0.5 * driving_pattern
        self.state['throttle_base'] = max(0, min(100, 
            self.state['throttle_base'] + random.uniform(-5, 5) * throttle_variation))
        
        # RPM baseado no throttle e tipo de veículo
        rpm_multiplier = 1.0
        if self.vehicle_type == VehicleType.SPORT:
            rpm_multiplier = 1.5
        elif self.vehicle_type == VehicleType.PICKUP:
            rpm_multiplier = 1.2
            
        target_rpm = (self.state['throttle_base'] / 100) * self.config['rpm_range'][1] * rpm_multiplier
        self.state['rpm_base'] = self.state['rpm_base'] * 0.9 + target_rpm * 0.1
        self.state['rpm_base'] += random.uniform(-50, 50)
        
        # 2. Velocidade correlacionada com RPM e marcha
        speed_from_rpm = (self.state['rpm_base'] / 3000) * self.config['speed_range'][1]
        self.state['speed_base'] = self.state['speed_base'] * 0.95 + speed_from_rpm * 0.05
        self.state['speed_base'] += random.uniform(-2, 2)
        
        # 3. Marcha baseada na velocidade
        speed = self.state['speed_base']
        if speed < 20:
            self.state['gear'] = 1
        elif speed < 40:
            self.state['gear'] = 2
        elif speed < 60:
            self.state['gear'] = 3
        elif speed < 80:
            self.state['gear'] = 4
        elif speed < 100:
            self.state['gear'] = 5
        elif speed < 120:
            self.state['gear'] = 6
        else:
            self.state['gear'] = 7 if self.vehicle_type == VehicleType.SPORT else 6
        
        # 4. Temperatura correlacionada com RPM e velocidade
        temp_increase = (self.state['rpm_base'] / 3000) * 10
        cooling = (speed / 100) * 8  # Resfriamento pelo ar
        self.state['temp_base'] = self.config['temp_range'][0] + temp_increase - cooling
        self.state['temp_base'] += random.uniform(-1, 1)
        
        # 5. Tensão da bateria varia com uso
        voltage_drop = (self.state['rpm_base'] / 5000) * 0.5
        self.state['voltage_base'] = 13.8 - voltage_drop + random.uniform(-0.1, 0.1)
        
        # 6. Consumo de combustível
        fuel_consumption = (self.state['rpm_base'] / 3000) * 0.01
        self.state['fuel_level'] = max(0, self.state['fuel_level'] - fuel_consumption)
        
        # Aplicar limites
        self.state['rpm_base'] = max(self.config['rpm_range'][0], 
                                     min(self.config['rpm_range'][1], self.state['rpm_base']))
        self.state['speed_base'] = max(0, min(self.config['speed_range'][1], self.state['speed_base']))
        self.state['temp_base'] = max(self.config['temp_range'][0], 
                                     min(self.config['temp_range'][1] + 10, self.state['temp_base']))
        self.state['throttle_base'] = max(0, min(100, self.state['throttle_base']))
        self.state['voltage_base'] = max(11.5, min(14.5, self.state['voltage_base']))
        
        return ECUData(
            device_id=self.device_id,
            rpm=int(self.state['rpm_base']),
            speed=round(self.state['speed_base'], 1),
            temp_motor=round(self.state['temp_base'], 1),
            throttle_pos=round(self.state['throttle_base'], 1),
            voltage=round(self.state['voltage_base'], 2),
            gear=self.state['gear'],
            fuel_level=round(self.state['fuel_level'], 1),
            timestamp=datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        )
    
    def send_data(self, data: ECUData) -> bool:
        """
        Envia dados para o servidor Flask
        
        Args:
            data: Dados da ECU a serem enviados
            
        Retorna:
            True se o envio foi bem-sucedido, False caso contrário
        """
        try:
            # Adiciona campos obrigatórios para o servidor Flask
            payload = data.to_dict()
            
            # Renomeia campos para corresponder ao esperado pelo servidor
            payload['temp_motor'] = payload.pop('temp_motor')
            payload['throttle_pos'] = payload.pop('throttle_pos')
            
            headers = {'Content-Type': 'application/json'}
            
            # Simula latência de rede (opcional)
            network_latency = random.uniform(0.01, 0.1)
            time.sleep(network_latency)
            
            # Simula falhas aleatórias (1% de chance)
            if random.random() < 0.01:
                raise ConnectionError("Simulação de falha de conexão")
            
            response = requests.post(
                self.server_url,
                json=payload,
                headers=headers,
                timeout=2.0
            )
            
            if response.status_code == 200:
                logger.debug(f"Dispositivo {self.device_id}: Dados enviados com sucesso")
                return True
            else:
                logger.warning(f"Dispositivo {self.device_id}: Erro HTTP {response.status_code}")
                return False
                
        except requests.exceptions.ConnectionError:
            logger.error(f"Dispositivo {self.device_id}: Não foi possível conectar ao servidor")
            return False
        except requests.exceptions.Timeout:
            logger.warning(f"Dispositivo {self.device_id}: Timeout na conexão")
            return False
        except Exception as e:
            logger.error(f"Dispositivo {self.device_id}: Erro inesperado: {str(e)}")
            return False
    
    def run(self, interval: float = 2.0):
        """
        Loop principal do dispositivo
        
        Args:
            interval: Intervalo entre envios (em segundos)
        """
        self.running = True
        logger.info(f"Dispositivo {self.device_id} iniciado. Intervalo: {interval}s")
        
        consecutive_failures = 0
        max_consecutive_failures = 5
        
        while self.running:
            try:
                # Gera dados realistas
                ecu_data = self.generate_realistic_data()
                
                # Exibe dados no console (opcional)
                if random.random() < 0.1:  # Apenas 10% das vezes para não poluir
                    logger.info(f"{self.device_id}: RPM={ecu_data.rpm}, Vel={ecu_data.speed}km/h, "
                               f"Temp={ecu_data.temp_motor}°C, Comb={ecu_data.fuel_level}%")
                
                # Envia dados para o servidor
                success = self.send_data(ecu_data)
                
                if success:
                    consecutive_failures = 0
                else:
                    consecutive_failures += 1
                    if consecutive_failures >= max_consecutive_failures:
                        logger.warning(f"Dispositivo {self.device_id}: Muitas falhas consecutivas. "
                                      f"Aguardando 10 segundos...")
                        time.sleep(10)
                        consecutive_failures = 0
                
                # Aguarda o próximo envio
                time.sleep(interval)
                
            except KeyboardInterrupt:
                logger.info(f"Dispositivo {self.device_id}: Interrompido pelo usuário")
                break
            except Exception as e:
                logger.error(f"Dispositivo {self.device_id}: Erro no loop principal: {str(e)}")
                time.sleep(interval)  # Espera antes de tentar novamente
    
    def start(self, interval: float = 2.0):
        """Inicia o dispositivo em uma thread separada"""
        self.thread = threading.Thread(
            target=self.run,
            args=(interval,),
            name=f"ESP32-{self.device_id}",
            daemon=True
        )
        self.thread.start()
        logger.info(f"Dispositivo {self.device_id} iniciado na thread {self.thread.name}")
    
    def stop(self):
        """Para a execução do dispositivo"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=5.0)
            logger.info(f"Dispositivo {self.device_id} parado")

class SimulatorManager:
    """Gerencia múltiplos dispositivos simulados"""
    
    def __init__(self):
        self.devices: Dict[str, ESP32Simulator] = {}
        self.running = False
    
    def create_devices(self, num_devices: int, server_url: str, 
                      vehicle_types: Optional[List[VehicleType]] = None) -> List[str]:
        """
        Cria múltiplos dispositivos simulados
        
        Args:
            num_devices: Número de dispositivos a criar
            server_url: URL do servidor Flask
            vehicle_types: Lista de tipos de veículos (se None, usa tipos aleatórios)
            
        Retorna:
            Lista de IDs dos dispositivos criados
        """
        if vehicle_types is None:
            vehicle_types = random.choices(list(VehicleType), k=num_devices)
        elif len(vehicle_types) < num_devices:
            # Repete tipos se necessário
            vehicle_types = vehicle_types * (num_devices // len(vehicle_types) + 1)
            vehicle_types = vehicle_types[:num_devices]
        
        device_ids = []
        for i in range(num_devices):
            device_id = f"Truck_{i+1:03d}"
            vehicle_type = vehicle_types[i]
            
            device = ESP32Simulator(
                device_id=device_id,
                server_url=server_url,
                vehicle_type=vehicle_type
            )
            
            self.devices[device_id] = device
            device_ids.append(device_id)
        
        logger.info(f"Criados {num_devices} dispositivos simulados")
        return device_ids
    
    def start_all(self, interval: float = 2.0):
        """Inicia todos os dispositivos"""
        self.running = True
        for device_id, device in self.devices.items():
            device.start(interval)
        logger.info(f"Todos os {len(self.devices)} dispositivos iniciados")
    
    def stop_all(self):
        """Para todos os dispositivos"""
        self.running = False
        for device_id, device in self.devices.items():
            device.stop()
        logger.info("Todos os dispositivos parados")
    
    def get_status(self) -> Dict[str, str]:
        """Retorna o status de todos os dispositivos"""
        status = {}
        for device_id, device in self.devices.items():
            status[device_id] = "RUNNING" if device.running else "STOPPED"
        return status
    
    def monitor(self, duration: Optional[float] = None):
        """
        Monitora a execução dos dispositivos
        
        Args:
            duration: Duração do monitoramento em segundos (None = indefinido)
        """
        start_time = time.time()
        
        try:
            while self.running:
                if duration and (time.time() - start_time) > duration:
                    logger.info("Duração de monitoramento atingida")
                    break
                
                # Exibe status a cada 10 segundos
                time.sleep(10)
                active_devices = sum(1 for d in self.devices.values() if d.running)
                logger.info(f"Monitor: {active_devices}/{len(self.devices)} dispositivos ativos")
                
        except KeyboardInterrupt:
            logger.info("Monitoramento interrompido pelo usuário")
        finally:
            self.stop_all()

def parse_arguments():
    """Analisa argumentos da linha de comando"""
    parser = argparse.ArgumentParser(
        description='Simulador de múltiplos dispositivos ESP32 para telemetria automotiva',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Exemplos:
  %(prog)s --devices 3                       # Simula 3 dispositivos
  %(prog)s --devices 5 --interval 1          # Dispositivos enviam a cada 1 segundo
  %(prog)s --devices 2 --server http://192.168.1.100:5000/data
  %(prog)s --devices 4 --types sedan,suv     # Tipos específicos de veículos
  
Tipos de veículos disponíveis: sedan, suv, pickup, hatch, sport
        """
    )
    
    parser.add_argument(
        '--devices', '-d',
        type=int,
        default=3,
        help='Número de dispositivos a simular (padrão: 3)'
    )
    
    parser.add_argument(
        '--interval', '-i',
        type=float,
        default=2.0,
        help='Intervalo entre envios em segundos (padrão: 2.0)'
    )
    
    parser.add_argument(
        '--server', '-s',
        type=str,
        default='http://localhost:5000/data',
        help='URL do servidor Flask (padrão: http://localhost:5000/data)'
    )
    
    parser.add_argument(
        '--types', '-t',
        type=str,
        default='',
        help='Tipos de veículos (separados por vírgula): sedan,suv,pickup,hatch,sport'
    )
    
    parser.add_argument(
        '--duration', '-D',
        type=float,
        help='Duração da simulação em segundos (execução indefinida se não especificado)'
    )
    
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Modo verbose (mostra mais informações)'
    )
    
    return parser.parse_args()

def main():
    """Função principal"""
    args = parse_arguments()
    
    # Configura nível de logging
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    
    # Parse dos tipos de veículos
    vehicle_types = []
    if args.types:
        type_map = {
            'sedan': VehicleType.SEDAN,
            'suv': VehicleType.SUV,
            'pickup': VehicleType.PICKUP,
            'hatch': VehicleType.HATCH,
            'sport': VehicleType.SPORT
        }
        
        for type_str in args.types.split(','):
            type_str = type_str.strip().lower()
            if type_str in type_map:
                vehicle_types.append(type_map[type_str])
            else:
                logger.warning(f"Tipo de veículo desconhecido: {type_str}")
    
    if vehicle_types and len(vehicle_types) < args.devices:
        logger.warning(f"Apenas {len(vehicle_types)} tipos especificados para {args.devices} dispositivos. "
                      f"Usando tipos aleatórios para os restantes.")
    
    # Validações
    if args.devices < 1:
        logger.error("Número de dispositivos deve ser pelo menos 1")
        return
    
    if args.interval < 0.5:
        logger.warning("Intervalo muito curto (< 0.5s) pode sobrecarregar o servidor")
    
    # Cria e inicia o gerenciador
    manager = SimulatorManager()
    
    try:
        # Cria dispositivos
        device_ids = manager.create_devices(
            num_devices=args.devices,
            server_url=args.server,
            vehicle_types=vehicle_types if vehicle_types else None
        )
        
        logger.info("=" * 60)
        logger.info(f"SIMULADOR ESP32 - INICIANDO")
        logger.info(f"Dispositivos: {args.devices}")
        logger.info(f"Intervalo: {args.interval}s")
        logger.info(f"Servidor: {args.server}")
        logger.info(f"Duração: {args.duration or 'Indefinida'}s")
        logger.info("=" * 60)
        
        # Inicia todos os dispositivos
        manager.start_all(interval=args.interval)
        
        # Inicia monitoramento
        manager.monitor(duration=args.duration)
        
    except KeyboardInterrupt:
        logger.info("\nSimulação interrompida pelo usuário")
    except Exception as e:
        logger.error(f"Erro na simulação: {str(e)}")
    finally:
        manager.stop_all()
        logger.info("Simulação finalizada")

if __name__ == "__main__":
    main()