.PHONY: create-veth
create-veth:
	ip link add veth1 type veth peer name veth1_p
	ip addr add 192.168.1.1/24 dev veth1
	ip addr add 192.168.1.2/24 dev veth1_p
	ip link set veth1 up
	ip link set veth1_p up
	ifconfig

.PHONY: setting
setting:
	echo 0 > /proc/sys/net/ipv4/conf/veth1/rp_filter
	echo 0 > /proc/sys/net/ipv4/conf/veth1_p/rp_filter
	echo 1 > /proc/sys/net/ipv4/conf/veth1/accept_local
	echo 1 > /proc/sys/net/ipv4/conf/veth1_p/accept_local

.PHONY: ping
ping:
	ping 192.168.1.2 -I veth1


.PHONY: clean
clean:
	ip link delete veth1
	ip link delete veth1_p