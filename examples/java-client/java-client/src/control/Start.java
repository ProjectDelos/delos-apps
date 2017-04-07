package control;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.Map;
import java.util.Queue;
import java.util.Random;

import client.ProxyClient;
import client.WriteID;

public class Start {

	private ProxyClient 					_client;
	private Map<WriteID, Integer>			_pending_appends;
	private int 							_window_sz;
	
	public Start(int port, int window_sz) throws IOException {
		_client = new ProxyClient(port);
		_pending_appends = new HashMap<WriteID, Integer>();
		_window_sz = window_sz;
	}
	
	public void test_values() throws IOException {
		assert _pending_appends.size() == 0;
		
		int num_requests = 100000;
		Random rand = new Random();
		byte[] colors = new byte[1];
		colors[0] = 1;
		
		byte[] get_data = new byte[4096];
		byte[] get_colors = new byte[512];
		
		int num_pending = 0;
		WriteID ack_wid = new WriteID();
		
		LinkedList<byte[]> inserted = new LinkedList<byte[]>();
		for (int i = 0; i < num_requests; ++i) {
			WriteID wid = new WriteID();
			byte[] payload = ByteBuffer.allocate(4).putInt(rand.nextInt()).array();
			
			inserted.add(payload);
			_client.async_append(colors, payload, wid);
			
			_pending_appends.put(wid, i);
			num_pending += 1;
			
			if (num_pending == _window_sz) {
				_client.wait_any_append(ack_wid);
				_pending_appends.remove(ack_wid);
					num_pending -= 1;
			}
		}
		
		while (num_pending != 0) {
			_client.wait_any_append(ack_wid);
			_pending_appends.remove(ack_wid);
			num_pending -= 1;
		}
		
		assert _pending_appends.size() == 0;
		
		int num_gets = 0;
		_client.snapshot();
		int[] num_results = new int[2];
		while (_client.get_next(get_data,  get_colors, num_results) == true) {
			byte[] cur = inserted.removeFirst();
			assert cur.length == num_results[0];
			for (int i = 0; i < cur.length; ++i) {
				assert cur[i] == get_data[i];
			}
			num_gets += 1;
		}
		assert num_gets == num_requests;
	}
	
	public void test_proxy() throws IOException {
		int num_requests = 100000;
		int num_pending = 0;
		
		byte[] payload = new byte[4];
		payload[0] = (byte)0xFF;
		payload[1] = (byte)0xFF;
		payload[2] = (byte)0xFF;
		payload[3] = (byte)0xFF;
		
		byte[] colors = new byte[1];
		colors[0] = 1;
		
		long start_time = System.nanoTime();
		
		WriteID ack_wid = new WriteID(0,0);
		
		Queue<WriteID> wid_list = new LinkedList<WriteID>();
		for (int i = 0; i < num_requests; ++i) {
			
			WriteID wid = new WriteID();
			_client.async_append(colors, payload, wid);
			
			_pending_appends.put(wid,  i);
			num_pending += 1;
			
			
			if (num_pending == _window_sz) {
				_client.wait_any_append(ack_wid);
				_pending_appends.remove(ack_wid);
					num_pending -= 1;
			}
		}
		
		while (num_pending != 0) {
			_client.wait_any_append(ack_wid);
			_pending_appends.remove(ack_wid);
			num_pending -= 1;
		}
		
		assert _pending_appends.size() == 0;
		
		long end_time = System.nanoTime();
		
		double throughput = num_requests*1000000000.0 / (end_time - start_time);
		System.err.println("Append throughput: " + throughput);
		
		byte[] get_data = new byte[4096];
		byte[] get_colors = new byte[512];
		int[] num_results = new int[2];
		
		int num_gets = 0;
		
		start_time = System.nanoTime();
		
		_client.snapshot();
		while (_client.get_next(get_data, get_colors, num_results) == true) 
			num_gets += 1;
		
		end_time = System.nanoTime();
		
		assert num_gets == num_requests;
		throughput = num_requests*1000000000.0 / (end_time - start_time);
		System.err.println("Get throughput: " + throughput);
	}
	
	public static void main(String[] args) {
		int port = Integer.parseInt(args[0]);
		
		try {
			Start st = new Start(port, 48);
			st.test_proxy();
			st.test_values();
		} catch (IOException e) {
			System.err.println("Network error!");
		}
	}
}
