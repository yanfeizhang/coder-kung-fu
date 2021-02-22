import java.net.Socket;
import java.net.InetSocketAddress;
 
public class Client {
    public static final int MAX_CONNECTION_NUM = 50000;

    public static void main(String[] args) throws Exception {
        if(2 != args.length){
            System.out.println("Usage: java Client <server ip> <server port>\n");
            return;
        }

        //1. 从命令行获取并解析local ip、server ip以及端口
        String sIp = args[0];
        int sPort = Integer.parseInt(args[1]);

        //2. 开始建立连接
        //用数组将 socket 保存起来，防止连接被过早释放
        Socket[] sockets = new Socket[MAX_CONNECTION_NUM];  
        for(int i = 1; i <= MAX_CONNECTION_NUM; i++){
            try {
                Socket s = new Socket();
                s.connect(new InetSocketAddress(sIp, sPort));

                if(false == s.isConnected()){
                    System.out.println(" 连接 "+sIp+":"+sPort+" 失败！ ");
                    return;
                }
                sockets[i-1] = s;

            } catch (Exception e) {
                //连接失败则小憩一会儿接着连
                Thread.sleep(500);
                e.printStackTrace();
            }

            //稍稍停顿一下，避免把服务端的握手队列打满
            if(0 == i % 500){
                Thread.sleep(500);
                System.out.println("连接 "+sIp+":"+sPort+" 成功了 "+i+" 条");
            }
        }
      
        //把所有连接都 hold 一会儿，方便观察
        Thread.sleep(300 * 1000);

        //3. 释放所有的连接
        System.out.println("关闭所有的连接...\n");
        for(int i = 0; i < MAX_CONNECTION_NUM; i++){
            sockets[i].close();
        }
   }
}