package client;

import java.io.*;
import java.net.*;
import java.util.Queue;

public class ProxyClient {

	private int 				_server_port;
	private Socket 				_socket;
	private DataOutputStream 		_output;
	private DataInputStream 		_input;
	private int[] 				_empty_causal;
	
	private void serialize_async_append(int[] append_colors, int[] depend_colors, byte[] payload) throws IOException {
		_output.writeInt(0);
		_output.writeInt(append_colors.length);
		_output.writeInt(depend_colors.length);
		
		for (int i = 0; i < append_colors.length; ++i) {
			_output.writeInt(append_colors[i]);
		}
		
		for (int i = 0; i < depend_colors.length; ++i) {
			_output.writeInt(depend_colors[i]);
		}
		
		_output.writeInt(payload.length);
		_output.write(payload);
		
		_output.flush();
	}
	
	private void deserialize_async_append_response(WriteID wid) throws IOException {
		long part1 = _input.readLong();
		long part2 = _input.readLong();
		wid.initialize(part1, part2);
	}
	
	private void serialize_wait_any() throws IOException {
		_output.writeInt(2);
		_output.flush();
	}
	
	private void deserialize_wait_any_response(WriteID wid) throws IOException {
		long part1 = _input.readLong();
		long part2 = _input.readLong();
		wid.initialize(part1, part2);
	}
	
	private void serialize_try_wait_any() throws IOException {
		_output.writeInt(1);
		_output.flush();
	}
	
	private void deserialize_try_wait_any_response(Queue<WriteID> wid_list) throws IOException {
		wid_list.clear();
		int num_acks = _input.readInt();
		
		for (int i = 0; i < num_acks; ++i) {
			long part1 = _input.readLong();
			long part2 = _input.readLong();
			
			WriteID wid = new WriteID(part1, part2);
			wid_list.add(wid);
		}
	}
	
	private void serialize_snapshot() throws IOException {
		_output.writeInt(3);
		_output.flush();
	}
	
	private void deserialize_snapshot() throws IOException {
		_input.readInt();
	}
	
	private void serialize_get_next() throws IOException {
		_output.writeInt(4);
		_output.flush();
	}
	
	private boolean deserialize_get_next_response(byte[] data_buf, int[] color_buf, int[] num_results) throws IOException {
		int buf_sz = _input.readInt();
		int colors_sz = _input.readInt();
		
		num_results[0] = buf_sz;
		num_results[1] = colors_sz;
		if (colors_sz == 0 && buf_sz == 0) {
			return false;
		} else if (buf_sz == 0) {
			return true;
		} else {
			_input.read(data_buf, 0, buf_sz);
			for (int i = 0; i < colors_sz; ++i) {
				color_buf[i] = _input.readInt();
			}
			return true;
		}
	}
	
	public ProxyClient(int server_port) throws IOException {
		this("127.0.0.1", server_port);
	}
	
	public ProxyClient(String hostname, int server_port) throws IOException {
		_server_port = server_port;
		_socket = new Socket(hostname, _server_port);
		_input = new DataInputStream(new BufferedInputStream(_socket.getInputStream()));
		_output = new DataOutputStream(new BufferedOutputStream(_socket.getOutputStream()));
		_empty_causal = new int[0];
	}
	
	public void async_append(int[] append_colors, byte[] buffer, WriteID wid)  throws IOException{
		serialize_async_append(append_colors, _empty_causal, buffer);
		deserialize_async_append_response(wid);
	}
	
	public void async_append_causal(int[] append_colors, int[] causal_colors, byte[] buffer, WriteID wid) throws IOException {
		serialize_async_append(append_colors, causal_colors, buffer);
		deserialize_async_append_response(wid);
	}
	
	public void wait_any_append(WriteID ack_wid) throws IOException {
		serialize_wait_any();
		deserialize_wait_any_response(ack_wid);
	}
	
	public void try_wait_any_append(Queue<WriteID> wid_list) throws IOException {
		serialize_try_wait_any();
		deserialize_try_wait_any_response(wid_list);
	}
	
	public void snapshot() throws IOException {
		serialize_snapshot();
		deserialize_snapshot();
	}
	
	public boolean get_next(byte[] data, int[] colors, int[] num_results) throws IOException {
		serialize_get_next();
		return deserialize_get_next_response(data, colors, num_results);
	}
}
