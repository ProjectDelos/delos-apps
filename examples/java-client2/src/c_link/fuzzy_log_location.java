package c_link;

import com.sun.jna.Pointer;
import com.sun.jna.Structure;
import java.util.Arrays;
import java.util.List;

/**
 * <i>native declaration : fuzzy_log.h:15</i><br>
 * This file was autogenerated by <a href="http://jnaerator.googlecode.com/">JNAerator</a>,<br>
 * a tool written by <a href="http://ochafik.com/">Olivier Chafik</a> that <a href="http://code.google.com/p/jnaerator/wiki/CreditsAndLicense">uses a few opensource projects.</a>.<br>
 * For help, please visit <a href="http://nativelibs4java.googlecode.com/">NativeLibs4Java</a> , <a href="http://rococoa.dev.java.net/">Rococoa</a>, or <a href="http://jna.dev.java.net/">JNA</a>.
 */
public class fuzzy_log_location extends Structure {
	/** C type : ColorID */
	public int color;
	/** C type : LocationInColor */
	public int entry;

	public fuzzy_log_location() {
		super();
	}

	protected List<String> getFieldOrder() {
		return Arrays.asList("color", "entry");
	}

	/**
	 * @param color C type : ColorID<br>
	 * @param entry C type : LocationInColor
	 */
	public fuzzy_log_location(int color, int entry) {
		super();
		this.color = color;
		this.entry = entry;
	}

	public fuzzy_log_location(Pointer peer) {
		super(peer);
	}

	public static class ByReference extends fuzzy_log_location implements Structure.ByReference {

	};

	public static class ByValue extends fuzzy_log_location implements Structure.ByValue {

	};
}
