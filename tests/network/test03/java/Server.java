import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.InetSocketAddress;
 
public class Server {
    
    public static final int MAX_CONNECTION_NUM = 1100000;

    public static void main(String[] args) {
        if(2 != args.length){
            System.out.println("Usage: java Server <server ip> <server port>\n");
            return;
        }

        String sIp = args[0];
        int sPort = Integer.parseInt(args[1]);

        //用数组将 socket 保存起来，防止连接被过早回收
        Socket[] sockets = new Socket[MAX_CONNECTION_NUM];

        try {
            ServerSocket ss = new ServerSocket();
            ss.bind(new InetSocketAddress(sIp, sPort), 1024);
            System.out.println("启动Server " + sIp + ":" + sPort + "...");

            int i = 0;
            while(true){
                try{
                    Socket s = ss.accept();
                    sockets[i] = s;
                    i++;
                    System.out.println("Server " + sIp + ":" + sPort + " 接收到第 " + i +" 条连接！");
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}
