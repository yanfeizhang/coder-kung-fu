.PHONY: create-net1
create-net1:
	ip netns add net1
	ip link add veth1 type veth peer name veth1_p
	ip link set veth1 netns net1
	ip netns exec net1 ip addr add 192.168.0.2/24 dev veth1  # IP
	ip netns exec net1 ip link set veth1 up
	ip netns exec net1 ip link list
	ip netns exec net1 ifconfig
	ip netns exec net1 route add default gw 192.168.0.1 veth1 # 默认网关
	ip netns exec net1 route -n

.PHONY: create-br
create-br:
	brctl addbr br0
	ip addr add 192.168.0.1/24 dev br0
	ip link set dev veth1_p master br0
	ip link set veth1_p up
	ip link set br0 up
	brctl show

.PHONY: create-nat
create-nat:	
	sysctl net.ipv4.conf.all.forwarding=1
	iptables -P FORWARD ACCEPT
	iptables -t nat -A POSTROUTING -s 192.168.0.0/24 ! -o br0 -j MASQUERADE
	iptables -t nat -A PREROUTING  ! -i br0 -p tcp -m tcp --dport 8088 -j DNAT --to-destination 192.168.0.2:80
	iptables-save		
	
.PHONY: clean
clean:
	ip link delete br0
	ip link delete veth1_p
	ip link list 
	ip netns del net1
	ip netns list
	iptables -t nat -L -n --line-numbers
	iptables -t nat -D PREROUTING 1  # TODO：待优化
	iptables -t nat -D POSTROUTING 1  # TODO：待优化
	iptables-save
