.PHONY: create-net1
create-net1:
	ip netns add net1
	ip link add veth1 type veth peer name veth1_p
	ip link set veth1 netns net1
	ip netns exec net1 ip addr add 192.168.0.101/24 dev veth1
	ip netns exec net1 ip link set veth1 up
	ip netns exec net1 ip link list
	ip netns exec net1 ifconfig
	ip netns list

.PHONY: create-net2
create-net2:
	ip netns add net2
	ip link add veth2 type veth peer name veth2_p
	ip link set veth2 netns net2
	ip netns exec net2 ip addr add 192.168.0.102/24 dev veth2
	ip netns exec net2 ip link set veth2 up
	ip netns exec net2 ip link list
	ip netns exec net2 ifconfig
	ip netns list

.PHONY: create-br
create-br:
	brctl addbr br0
	ip addr add 192.168.0.100/24 dev br0
	ip link set dev veth1_p master br0
	ip link set dev veth2_p master br0
	ip link set veth1_p up
	ip link set veth2_p up
	ip link set br0 up
	brctl show
	
.PHONY: clean
clean:
	ip link delete br0
	ip link delete veth1_p
	ip link delete veth2_p
	ip link list 
	ip netns del net1
	ip netns del net2
	ip netns list
