# yenc_decode_stream - Streaming yEnc Decoder with Header/Footer Parsing

The `yenc_decode_stream` function provides high-performance streaming yEnc decoding with automatic header and footer parsing. It's designed for processing NNTP article data incrementally without loading entire articles into memory.

## Function Signature

```python
yenc_decode_stream(data, decoder_state=None) -> (decoded_data, state_dict)
```

## Parameters

- **data** (memoryview or bytes): Raw NNTP article data chunk
  - **First call**: Must be at least 1024 bytes to ensure complete headers
  - **Subsequent calls**: Any size
- **decoder_state** (dict, optional): State dictionary from previous call (None for first call)

## Return Values

Returns a tuple of `(decoded_data, state_dict)`:

### decoded_data (bytearray)
The decoded binary data from this chunk.

### state_dict (dict)
A dictionary containing:

| Key | Type | Description |
|-----|------|-------------|
| `decoder_state` | int | Internal decoder state (for continuation) |
| `crc` | int | Accumulated CRC32 value |
| `filename` | str | Filename from =ybegin header |
| `file_size` | int | Total file size from =ybegin header |
| `part_begin` | int or None | Part start offset (0-based, from =ypart) |
| `part_size` | int or None | Part size (from =ypart) |
| `crc_expected` | int or None | Expected CRC from =yend footer |
| `crc_correct` | bool or None | CRC validation result (True/False/None) |
| `done` | bool | True when footer has been parsed |

## Usage Examples

### Example 1: Single-Shot Decoding

Decode a complete yEnc article in one call:

```python
import sabctools

# Read complete NNTP article
with open('article.yenc', 'rb') as f:
    article_data = f.read()

# Decode (must be >= 1024 bytes)
decoded, state = sabctools.yenc_decode_stream(memoryview(article_data))

print(f"Filename: {state['filename']}")
print(f"File size: {state['file_size']}")
print(f"Decoded: {len(decoded)} bytes")
print(f"CRC correct: {state['crc_correct']}")
print(f"Complete: {state['done']}")
```

### Example 2: Streaming from Network

Process NNTP article data as it arrives from the network:

```python
import sabctools

def download_and_decode_article(socket, article_id):
    """Download and decode yEnc article from NNTP server"""
    buffer = bytearray()
    state = None
    decoded_parts = []
    
    while True:
        chunk = socket.recv(8192)
        if not chunk:
            break
        
        buffer.extend(chunk)
        
        # First call needs >= 1024 bytes
        if state is None and len(buffer) < 1024:
            continue
        
        try:
            decoded, state = sabctools.yenc_decode_stream(
                memoryview(buffer),
                state
            )
            
            if decoded:
                decoded_parts.append(decoded)
            
            # Clear buffer after processing
            buffer.clear()
            
            # Check if done
            if state['done']:
                break
                
        except ValueError as e:
            if "requires at least 1024 bytes" in str(e):
                continue  # Wait for more data
            raise
    
    # Combine all decoded chunks
    full_data = b''.join(decoded_parts)
    
    return full_data, state

# Usage
# decoded_data, state = download_and_decode_article(nntp_socket, "12345")
# print(f"Downloaded: {state['filename']}, CRC OK: {state['crc_correct']}")
```

### Example 3: Processing Buffered Chunks

Process large articles in manageable chunks with buffering:

```python
import sabctools

def decode_article_chunked(article_data, chunk_size=65536):
    """Decode article in chunks for memory efficiency"""
    state = None
    decoded_parts = []
    offset = 0
    
    while offset < len(article_data):
        # Get next chunk
        chunk_end = min(offset + chunk_size, len(article_data))
        chunk = article_data[offset:chunk_end]
        
        # First call must have >= 1024 bytes
        if state is None and len(chunk) < 1024:
            # Read more data for first chunk
            chunk_end = min(offset + max(chunk_size, 1024), len(article_data))
            chunk = article_data[offset:chunk_end]
        
        # Decode chunk
        decoded, state = sabctools.yenc_decode_stream(
            memoryview(chunk),
            state
        )
        
        if decoded:
            decoded_parts.append(bytes(decoded))
        
        offset = chunk_end
        
        # Stop if we found the footer
        if state['done']:
            break
    
    return b''.join(decoded_parts), state

# Usage
with open('large_article.yenc', 'rb') as f:
    article_data = f.read()

decoded, state = decode_article_chunked(article_data)
print(f"File: {state['filename']}")
print(f"Size: {len(decoded)} bytes")
print(f"CRC: {'OK' if state['crc_correct'] else 'FAILED'}")
```

### Example 4: Multi-Part Article Processing

Handle multi-part yEnc files:

```python
import sabctools

def decode_multipart_article(article_data):
    """Decode a multi-part yEnc article"""
    decoded, state = sabctools.yenc_decode_stream(memoryview(article_data))
    
    # Check if it's a multi-part file
    if state['part_begin'] is not None:
        print(f"Multi-part file detected:")
        print(f"  Filename: {state['filename']}")
        print(f"  Total size: {state['file_size']}")
        print(f"  Part offset: {state['part_begin']}")
        print(f"  Part size: {state['part_size']}")
        print(f"  CRC correct: {state['crc_correct']}")
        
        # Return with offset information for assembly
        return {
            'data': decoded,
            'offset': state['part_begin'],
            'size': state['part_size'],
            'filename': state['filename'],
            'crc_ok': state['crc_correct']
        }
    else:
        print(f"Single-part file: {state['filename']}")
        return {
            'data': decoded,
            'offset': 0,
            'size': len(decoded),
            'filename': state['filename'],
            'crc_ok': state['crc_correct']
        }

# Usage
# part_info = decode_multipart_article(article_data)
# Write to file at correct offset:
# with open(part_info['filename'], 'r+b') as f:
#     f.seek(part_info['offset'])
#     f.write(part_info['data'])
```

### Example 5: Error Handling

Proper error handling for various failure cases:

```python
import sabctools

def safe_decode_stream(article_data):
    """Decode with comprehensive error handling"""
    try:
        # Check minimum size for first call
        if len(article_data) < 1024:
            return None, f"Article too small: {len(article_data)} bytes (need >= 1024)"
        
        # Decode
        decoded, state = sabctools.yenc_decode_stream(memoryview(article_data))
        
        # Validate results
        if not state['done']:
            return None, "Article incomplete: footer not found"
        
        if not state.get('filename'):
            return None, "Invalid article: filename not found"
        
        if state['crc_correct'] is False:
            return None, f"CRC mismatch: expected {state['crc_expected']:08x}, got {state['crc']:08x}"
        
        return decoded, None
        
    except ValueError as e:
        return None, f"Decode error: {e}"
    except Exception as e:
        return None, f"Unexpected error: {e}"

# Usage
decoded, error = safe_decode_stream(article_data)
if error:
    print(f"Failed: {error}")
else:
    print(f"Success: {len(decoded)} bytes decoded")
```

## Performance Considerations

1. **First Call Size**: Ensure first call has at least 1024 bytes for header parsing
2. **Chunk Size**: Larger chunks (64KB-256KB) provide better performance
3. **Memory Views**: Use `memoryview()` to avoid copying data
4. **GIL Release**: Decoder releases Python GIL during decoding for better multi-threading
5. **SIMD**: Automatically uses CPU SIMD instructions (SSE2/AVX2/AVX-512/NEON/RVV)

## Comparison with yenc_decode

| Feature | `yenc_decode` | `yenc_decode_stream` |
|---------|---------------|----------------------|
| Input | Complete article | Streaming chunks |
| Min size | Any | 1024 bytes (first call) |
| Header parsing | Yes | Yes |
| Footer parsing | Yes | Yes |
| Streaming | No | Yes |
| State management | None | Manual (dict) |
| Memory usage | Full article | Chunk-based |
| CRC validation | Automatic | Automatic |
| Return format | Tuple (6 values) | Tuple (2 values) |

## Error Conditions

The function raises `ValueError` for:
- First call with < 1024 bytes
- Invalid or missing =ybegin header
- Missing filename in header
- Invalid input data format

The function raises `TypeError` for:
- Non-memoryview input data
- Non-dict decoder_state (on continuation calls)

## Technical Details

- **Header Parsing**: Automatically parses =ybegin and optional =ypart headers
- **Footer Detection**: Searches for \r\n=yend in each chunk
- **CRC Calculation**: Accumulates CRC32 across all chunks
- **State Preservation**: All metadata stored in returned dictionary
- **Raw Mode**: Always uses raw mode with dot-unstuffing for NNTP compliance
- **End Detection**: Automatically detects =yend footer and validates CRC

## Notes

- The 1024-byte minimum ensures =ybegin and =ypart headers are complete
- State dictionary can be passed directly to subsequent calls
- CRC is accumulated automatically across chunks
- Part offsets are 0-based (converted from 1-based =ypart values)
- Filename encoding: attempts UTF-8, falls back to Latin-1
