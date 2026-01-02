#!/bin/bash

echo "⚡ SETTING UP ULTRA LOW LATENCY SYSTEM ⚡"
echo ""

# Устанавливаем приоритет реального времени
echo "Setting real-time priority..."
sudo sysctl -w net.core.rmem_max=26214400
sudo sysctl -w net.core.wmem_max=26214400
sudo sysctl -w net.core.rmem_default=26214400
sudo sysctl -w net.core.wmem_default=26214400
sudo sysctl -w net.ipv4.tcp_rmem="4096 87380 26214400"
sudo sysctl -w net.ipv4.tcp_wmem="4096 65536 26214400"
sudo sysctl -w net.core.netdev_max_backlog=5000

# Отключаем power saving для сетевой карты
echo "Disabling network power saving..."
sudo ethtool -c $(ip route show default | awk '/default/ {print $5}') | grep -q "rx-usecs:"
if [ $? -eq 0 ]; then
    sudo ethtool -C $(ip route show default | awk '/default/ {print $5}') rx-usecs 0 tx-usecs 0
fi

# Настройка IRQ балансировки
echo "Setting IRQ affinity..."
sudo apt-get install -y irqbalance
sudo systemctl enable irqbalance
sudo systemctl start irqbalance

# Приоритет для аудио
echo "Setting audio priority..."
pactl set-sink-volume @DEFAULT_SINK@ 100%
pactl set-source-volume @DEFAULT_SOURCE@ 100%

# Сборка
echo ""
echo "Building ultra low latency voice chat..."
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

echo ""
echo "✅ Build complete!"
echo ""
echo "🎯 Target latency: < 30ms round-trip"
echo ""
echo "Usage:"
echo "  Server: sudo ./voice server    (needs sudo for SO_PRIORITY)"
echo "  Client: ./voice client <IP>"
echo ""
echo "Note: Run server with sudo for maximum network priority"
