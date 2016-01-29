##=============================================================================
 #
 # Copyright (C) 2003, 2011 Alessandro Duca <alessandro.duca@gmail.com>
 #
 # This library is free software; you can redistribute it and/or
 # modify it under the terms of the GNU Lesser General Public
 # License as published by the Free Software Foundation; either
 # version 2.1 of the License, or (at your option) any later version.
 #
 # This library is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 # Lesser General Public License for more details.
 #
 # You should have received a copy of the GNU Lesser General Public
 # License along with this library; if not, write to the Free Software
 # Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 #=============================================================================
 # 
##=============================================================================


import sys
from cStringIO import StringIO
import _yenc

E_ERROR		= 64
E_CRC32		= 65
E_PARMS		= 66

BIN_MASK        = 0xffffffffL

class Error(Exception):
	""" 	Class for specific yenc errors
	"""
	def __init__(self, value="", code=E_ERROR):
		self.value = value
		
	def __str__(self):
		return "yenc.Error: %s\n" % self.value, self.value


def _checkArgsType(file_in, file_out, bytez):
	""" 	Internal checkings, not to be used from outside this module.
	"""
	if bytez < 0: 
            raise Error("No. of bytes can't be negative", E_PARMS)
	if type(file_in) == str:
		if file_in == "-":
			if bytez == 0: raise Error("No. of bytes is 0 or not \
				specified while reading from stdin", E_PARMS)
			file_in = sys.stdin
		else: file_in = open(file_in,"rb")
	if type(file_out) == str:
		if file_out == "-": file_out = sys.stdout
		else: file_out = open(file_out,"wb")
	return file_in, file_out, bytez


def encode(file_in, file_out, bytez=0):
	"""	encode(file_in, file_out, bytez=0): write "bytez" encoded bytes from
		file_in to file_out, if "bytez" is 0 encodes bytez until EOF.
	"""
	file_in, file_out, bytez = _checkArgsType(file_in, file_out, bytez)
	encoded, crc32 = _yenc.encode(file_in, file_out, bytez)
	return encoded, "%08x" % (crc32 ^ BIN_MASK)


def decode(file_in, file_out, bytez=0, crc_in=""):
	""" 	decode(file_in, file_out, bytez=0): write "bytez" decoded bytes from
		file_in to file_out, if "bytez" is 0 decodes bytes until EOF.
	"""
	file_in, file_out, bytez = _checkArgsType(file_in, file_out, bytez)
	decoded, crc32 = _yenc.decode(file_in, file_out, bytez)
	crc_hex = "%08x" % (crc32 ^ BIN_MASK)
	if crc_in and not cmp(crc_hex, crc_in.lower()):
		raise Error("crc32 error", E_CRC32)
	else:
		return decoded, crc_hex


class Encoder:
	""" 	class Encoder: facility class for encoding one string at time
                Constructor accepts an optional "output_file" argument, file must be opened in write mode.
                When output_file is specified flush() will write the buffer content onto the
                and close() will flush and close the file. After close() further calls to feed() will fail.
	"""
	def __init__(self, output_file = None):
		self._buffer = StringIO()
		self._column = 0
		self._output_file = output_file
		self._crc = BIN_MASK
		self._encoded = 0
		self._feedable = True

	def __del__(self):
                if self._output_file is not None:
                    self.flush()
                    self.close()
	
	def feed(self, data):
		"""	Encode some data and write the encoded data 
			into the internal buffer.
		"""
		if not self._feedable:
			raise IOError("Encoding already terminated")

		encoded, self._crc, self._column = _yenc.encode_string(data, self._crc, self._column)
		self._encoded = self._encoded + len(encoded)
		self._buffer.write(encoded)
		return len(encoded)
	
	def terminate(self):
		"""	Appends the terminal CRLF sequence to the encoded data.
                        Further calls to feed() will fail.
		"""
		self._buffer.write("\r\n")
		self._feedable = False
	
	def flush(self):
		"""	Writes the content of the internal buffer on output_file.
		"""
		if self._output_file is None:
			raise ValueError("Output file is 'None'")

                self._output_file.write(self._buffer.getvalue())
                self._buffer = StringIO()

        def close(self):
                """     Flushes and closes output_file.
                        The output buffer IS NOT automatically written to the file.
                """
                if self._output_file is None:
                        raise ValueError("Output file is 'None'")

                self._output_file.flush()
                self._output_file.close()
                self._output_file = None
                self._feedable = False
	
	def getEncoded(self):
		"""	Returns the data in the internal buffer.
		"""
		if self._output_file is not None:
			raise ValueError("Output file is not 'None'")

                return self._buffer.getvalue()
	
	def getSize(self):
		"""	Returns the total number of encoded bytes (not the size of
			the buffer).
		"""
		return self._encoded

	def getCrc32(self):
		"""	Returns the calculated crc32 string for the clear data.
		"""
                return "%08x" % (self._crc ^ BIN_MASK)


class Decoder:
	""" class Decoder: facility class for decoding one string at time
            Constructor accepts an optional "output_file" argument, file must be opened in write mode.
            When output_file is specified flush() will write the buffer content onto the
            and close() will flush and close the file. After close() further calls to feed() will fail.
	"""
	def __init__(self, output_file = None):
		self._buffer = StringIO()
		self._escape = 0
		self._output_file = output_file
		self._crc = BIN_MASK
		self._decoded = 0
                self._feedable = True
	
	def __del__(self):
                if self._output_file is not None:
                    self.flush()
                    self.close()
	
	def feed(self, data):
		"""	Decode some data and write the decoded data 
			into the internal buffer.
		"""
		if not self._feedable:
			raise IOError("Decoding already terminated")

		decoded, self._crc, self._escape = _yenc.decode_string(data, self._crc, self._escape)
		self._decoded = self._decoded + len(decoded)
		self._buffer.write(decoded)
		return len(decoded)
	
	def flush(self):
		"""	Writes the content of the internal buffer on the file
			passed as argument to the constructor.
		"""
		if self._output_file is None:
			raise ValueError("Output file is 'None'")

                self._output_file.write(self._buffer.getvalue())
                self._buffer = StringIO()

        def close(self):
                """     Flushes and closes output_file.
                        The output file is flushed before closing, further calls to feed() will fail.
                """
		if self._output_file is None:
			raise ValueError("Output file is 'None'")

                self._output_file.flush()
                self._output_file.close()
                self._output_file = None
                self._feedable = False
	
	def getDecoded(self):
		"""	Returns the decoded data from the internal buffer.
                        If output_file is not None this is going to raise a ValueError.
		"""
		if self._output_file is not None:
			raise ValueError("Output file is not 'None'")

                return self._buffer.getvalue()

	def getSize(self):
		"""	Returns the total number of decoded bytes (not the size of
			the buffer).
		"""
		return self._decoded
	
	def getCrc32(self):
		"""	Returns the calculated crc32 string for the decoded data.
		"""
		return "%08x" % (self._crc ^ BIN_MASK) 

