.PHONY: create-net1
create-net1:
	ip netns add net1
	ip netns exec net1 iptables -L
	ip netns exec net1 ip link list
	ip netns exec net1 ifconfig

.PHONY: create-veth
create-veth:
	ip link add veth1 type veth peer name veth1_p
	ip link set veth1 netns net1
	ip link list
	ip netns exec net1 ip link list

.PHONY: start-veth
start-veth:
	ip addr add 192.168.0.100/24 dev veth1_p
	ip netns exec net1 ip addr add 192.168.0.101/24 dev veth1
	ip link set dev veth1_p up 
	ip netns exec net1 ip link set dev veth1 up 
	ifconfig
	ip netns exec net1 ifconfig
	
.PHONY: ping
ping:
	ip netns exec net1 ping 192.168.0.100 -I veth1

.PHONY: clean
clean:
	ip link delete veth1_p
	ip link list 
	ip netns del net1
	ip netns list
