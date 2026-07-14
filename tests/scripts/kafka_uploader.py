#!/usr/bin/env python3
"""
Kafka Message Uploader - Compact Version
Uploads previously downloaded Kafka messages with SASL/SSL support.
Preserves timestamps by default and handles binary FlatBuffers data.
"""
import sys

from confluent_kafka import Producer
import json
import argparse
import base64
import gzip
import pickle
import time
from pathlib import Path
from importlib.util import find_spec
from datetime import datetime

# Try to import zstandard for decompression
def has_zstd():
    if find_spec('zstandard') is not None:
        try:
            import zstandard
            return True
        except ImportError:
            return False
    return False


HAS_ZSTD = has_zstd()


def load_kafka_config(config_path):
    """Load and process Kafka configuration from JSON file"""
    with open(config_path, 'r') as f:
        config_data = json.load(f)
    
    # Flatten KafkaParms list to dict and convert types
    kafka_config = {}
    for param in config_data.get('KafkaParms', []):
        kafka_config.update(param)
    
    # Convert numeric strings and booleans
    processed = {}
    for key, value in kafka_config.items():
        if key.endswith(('.ms', '.bytes')) or key in ['retries', 'batch.size']:
            processed[key] = int(value) if str(value).isdigit() else value
        elif str(value).lower() in ['true', 'false']:
            processed[key] = str(value).lower() == 'true'
        else:
            processed[key] = value
    return processed


def detect_file_format(file_path: Path):
    """Detect file format based on magic bytes"""
    with open(file_path, 'rb') as f:
        header = f.read(16)  # Read first 16 bytes for magic number detection
    
    # Zstandard magic number: 0x28B52FFD (little endian) or various frame formats
    if header[:4] == b'\x28\xb5\x2f\xfd':
        return 'zstd'
    # Alternative zstd magic (big endian)
    elif header[:4] == b'\xfd\x2f\xb5\x28':
        return 'zstd'
    # Gzip magic number: 0x1f8b
    elif header[:2] == b'\x1f\x8b':
        return 'gzip'
    # Pickle magic numbers (various protocol versions)
    elif header[0:1] in [b'\x80', b'\x81', b'\x82', b'\x83', b'\x84', b'\x85']:  # Pickle protocols 2-5
        return 'pickle'
    # Legacy pickle format (protocol 0/1) starts with '(' for tuple or 'c' for class
    elif header[0:1] in [b'(', b'c', b'l', b'd', b'S', b'I']:  
        return 'pickle'
    # JSON-like content (starts with { or [)
    elif header.lstrip()[:1] in [b'{', b'[']:
        return 'json'
    else:
        return 'unknown'


def file_extension_format(input_file: Path):
    extension_format = None
    ext = input_file.suffix
    if ext == '.zst':
        extension_format = 'zstd'
    elif ext == '.gz':
        extension_format = 'gzip'
    elif ext == '.bin':
        extension_format = 'pickle'
    elif ext in ['.json', '.jsonl']:
        extension_format = 'json'
    return extension_format


def get_decompressor(input_file: Path):
    """Get the appropriate decompressor based on file extension AND content detection"""
    # First try extension-based detection for backwards compatibility
    extension_format = file_extension_format(input_file)
    
    # Then detect based on content
    content_format = detect_file_format(input_file)
    
    # Use content detection, but fall back to extension if content is ambiguous
    detected_format = content_format if content_format != 'unknown' else extension_format
    
    print(f"File analysis: extension suggests '{extension_format}', content suggests '{content_format}', using '{detected_format}'")
    
    if detected_format == 'zstd':
        if HAS_ZSTD:
            from zstandard import ZstdDecompressor
            dctx = ZstdDecompressor()
            return dctx.stream_reader(open(input_file, 'rb')), "Zstandard decompression (auto-detected)"
        else:
            raise ValueError("File contains zstd data but zstandard library not installed. Install with: pip install zstandard")
    elif detected_format == 'gzip':
        return gzip.open(input_file, 'rb'), "Gzip decompression (auto-detected)"
    else:
        return open(input_file, 'rb'), "no decompression (raw binary/pickle/json)"


def decode_timestamp_ms(value: int | str) -> int | None:
    # Convert ISO timestamp to milliseconds since epoch
    if isinstance(value, int):
        return value
    try:
        # Add midnight if only date provided
        print(f'{value = }')
        if 'T' not in value:
            value += 'T00:00:00'
        if value.endswith('Z'):
            value = value.replace('Z', '+00:00')
        timestamp_ms = int(datetime.fromisoformat(value).timestamp() * 1000)
    except (ValueError, AttributeError) as e:
        print(f"Warning: Could not parse timestamp '{value}': {e}")
        timestamp_ms = None
    return timestamp_ms


def json_loader(f):
    # For text mode, wrap binary streams
    if hasattr(f, 'mode') and 'b' in str(f.mode):
        import io
        f = io.TextIOWrapper(f, encoding='utf-8')

    for line_num, line in enumerate(f, 1):
        try:
            record = json.loads(line.strip())
            yield record
        except Exception as e:
            print(f"Error on line {line_num}: {e}")


def extract_json_message_data(record):
    # Extract and decode binary data
    key = record.get('key')
    if key and record.get('key_is_binary', False):
        key = base64.b64decode(key)
    elif key:
        key = key.encode('utf-8')

    value = record.get('value')
    if value and record.get('value_is_binary', False):
        value = base64.b64decode(value)
    elif value:
        value = value.encode('utf-8')

    # Handle timestamp preservation
    timestamp_ms = record.get('timestamp')
    partition = record.get('partition')

    return key, value, timestamp_ms, partition


def pickle_loader(f):
    while True:
        try:
            record = pickle.load(f)
            yield record
        except EOFError:
            break
        except Exception as e:
            print(f"Error loading pickle data: {e}")
            break


def extract_binary_message_data(record):
    # Binary format keys and values are already bytes
    key = record.get('key')
    value = record.get('value')
    timestamp_ms = record.get('timestamp')
    partition = record.get('partition')

    return key, value, timestamp_ms, partition


def _scan_file(input_file, message_loader, message_decoder) -> tuple[int, int, int, int]:
    """Scan the file for the number of messages, the number of partitions, and the earliest and latest timestamps in ms"""
    count = largest_partition_seen = 0
    earliest = int(datetime.fromisoformat("1970-01-01T00:00:00+00:00").timestamp() * 1000)
    latest = int(datetime.now().timestamp() * 1000)
    decompressor, decomp_info = get_decompressor(input_file)
    print(f"Using {decomp_info}")
    with decompressor as f:
        try:
            records = message_loader(f)
            if isinstance(records, int):
                raise ValueError("Message loader returned an integer instead of an iterable. Check the loader function.")
            for record in records:
                try:
                    key, value, timestamp_ms, partition = message_decoder(record)
                    count += 1
                    if partition is not None:
                        largest_partition_seen = max([partition, largest_partition_seen])
                    if timestamp_ms and (timestamp_ms := decode_timestamp_ms(timestamp_ms)):
                        earliest = min([earliest, timestamp_ms])
                        latest = max([latest, timestamp_ms])
                except Exception as e:
                    print(f"(scan) Error extracting message: {e}")
        except Exception as e:
            print(f"Error reading file: {e}")

    # The number of partitions is one more than the largest partition number -- hopefully
    return count, largest_partition_seen + 1, earliest, latest


def scan_file(input_file):
    """Scan the file for message count and partition info"""
    if file_extension_format(input_file) == 'json':
        return _scan_file(input_file, json_loader, extract_json_message_data)
    else:
        return _scan_file(input_file, pickle_loader, extract_binary_message_data)


class KafkaMessageUploader:
    """Uploads messages to Kafka with authentication and timestamp preservation"""
    
    def __init__(self, brokers, preserve_timestamps=True, kafka_config=None):
        self.preserve_timestamps = preserve_timestamps
        
        # Base producer config for confluent-kafka
        config = {
            'bootstrap.servers': brokers if isinstance(brokers, str) else ','.join(brokers),
            'acks': 'all',
            'retries': 3,
            'batch.size': 16384,
            'linger.ms': 10,
            'queue.buffering.max.kbytes': 32768,  # confluent-kafka equivalent of buffer.memory
        }
        
        # Apply Kafka config (confluent-kafka uses librdkafka config names directly)
        if kafka_config:
            self._apply_kafka_config(config, kafka_config)
        
        # Create producer with delivery callback
        self.producer = Producer(config)
        self.delivery_callback = self._delivery_callback
    
    def _apply_kafka_config(self, config, kafka_config):
        """Apply Kafka configuration to producer config"""
        # Most configs can be passed directly to confluent-kafka
        direct_configs = {
            'message.max.bytes', 'message.timeout.ms', 'queue.buffering.max.ms',
            'batch.size', 'retries', 'security.protocol', 'sasl.mechanism',
            'sasl.username', 'sasl.password', 'ssl.ca.location',
            'ssl.certificate.location', 'ssl.key.location', 'ssl.key.password',
            'ssl.check.hostname'
        }
        
        # Skip consumer-only configs
        skip = {'fetch.message.max.bytes', 'statistics.interval.ms', 'api.version.request', 'message.copy.max.bytes'}
        
        for key, value in kafka_config.items():
            if key not in skip and key in direct_configs:
                config[key] = value

    def _delivery_callback(self, err, msg):
        """Delivery callback for producer"""
        if err:
            print(f'Message delivery failed: {err}')
        # Silent on success to avoid spam

    def _process_upload(self, input_file, target_topic, message_loader, message_decoder, sample_every=1):
        """Common upload processing for both JSON and binary formats

        sample_every=N uploads only every Nth message (N=1 uploads everything).
        """
        skipped = skip_count = count = errors = 0

        decompressor, decomp_info = get_decompressor(input_file)
        print(f"Using {decomp_info}")

        with decompressor as f:
            try:
                for record in message_loader(f):
                    skip_count += 1
                    if sample_every > 1 and skip_count % sample_every != 0:
                        skipped += 1
                        continue

                    try:
                        # Extract message components
                        key, value, timestamp_ms, partition = message_decoder(record)
                        
                        # Prepare producer call arguments
                        produce_args = {
                            'topic': target_topic,
                            'key': key,
                            'value': value,
                            'callback': self.delivery_callback
                        }
                        
                        # Only add partition if it's not None
                        if partition is not None:
                            produce_args['partition'] = partition
                            
                        # Only add timestamp if it's not None and we're preserving timestamps
                        if timestamp_ms is not None and self.preserve_timestamps:
                            timestamp_ms = decode_timestamp_ms(timestamp_ms)
                            if timestamp_ms is not None:
                                produce_args['timestamp'] = timestamp_ms
                        
                        # Send message using confluent-kafka
                        self.producer.produce(**produce_args)
                        
                        count += 1
                        if count % 1000 == 0:
                            print(f"Uploaded {count} messages...", end="\r")
                            self.producer.flush()
                            
                    except Exception as e:
                        print(f"Error processing message: {e}")
                        errors += 1
                        
            except Exception as e:
                print(f"Error reading file: {e}")
                errors += 1
        
        # Final flush and status
        self.producer.flush()
        print(f"Upload completed: {count} messages sent ({skipped} skipped), {errors} errors")
        
        return count, errors
    
    def _extract_message_data(self, record):
        """Extract message components from record (to be overridden by format-specific logic)"""
        raise NotImplementedError("Subclasses must implement _extract_message_data")

    def upload_file(self, input_file, target_topic, sample_every=1):
        if file_extension_format(input_file) == 'json':
            return self.upload_from_jsonl(input_file, target_topic, sample_every)
        else:
            return self.upload_from_binary(input_file, target_topic, sample_every)

    def upload_from_jsonl(self, jsonl_file, target_topic, sample_every=1):
        """Upload messages from JSON Lines format"""
        # Temporarily override the extract method for JSON processing
        return self._process_upload(jsonl_file, target_topic, json_loader, extract_json_message_data, sample_every)

    def upload_from_binary(self, binary_file, target_topic, sample_every=1):
        """Upload messages from binary pickle format"""
        # Temporarily override the extract method for binary processing
        return self._process_upload(binary_file, target_topic, pickle_loader, extract_binary_message_data, sample_every)

    
    def batch_upload_with_rate_limit(self, input_file, target_topic, rate_limit, format_type='json'):
        """Upload with rate limiting (messages per second)"""
        print(f"Rate limiting to {rate_limit} messages/second")
        delay = 1.0 / rate_limit
        
        # Monkey patch the send method to add delay
        original_send = self.producer.send
        def rate_limited_send(*args, **kwargs):
            time.sleep(delay)
            return original_send(*args, **kwargs)
        self.producer.send = rate_limited_send
        
        # Upload using appropriate method
        if format_type == 'json':
            self.upload_from_jsonl(input_file, target_topic)
        elif format_type == 'binary':
            self.upload_from_binary(input_file, target_topic)


def make_parser():
    def path_like(arg):
        if isinstance(arg, Path):
            return arg
        return Path(arg)


    parser = argparse.ArgumentParser(description='Upload messages back to Kafka with authentication')
    parser.add_argument('--brokers', type=str, default='kafka1:9092', help='Kafka bootstrap servers (comma-separated)')
    parser.add_argument('--input', type=path_like, required=True, help='Input file to upload')
    parser.add_argument('--topic', type=str, required=True, help='Target Kafka topic')
    parser.add_argument('--format', choices=['json', 'binary'], default='json', help='Input format')
    parser.add_argument('--preserve-timestamps', action='store_true', default=True,
                        help='Preserve original message timestamps (default: True)')
    parser.add_argument('--no-preserve-timestamps', action='store_false', dest='preserve_timestamps',
                        help='Use current timestamps instead of original')
    parser.add_argument('--rate-limit', type=int, help='Limit upload rate (messages per second)')
    parser.add_argument('--kafka-config', help='Kafka configuration file (JSON format)')
    return parser


def parse_args(args) -> None | dict:
    # Show compression library status for supported file types
    if not HAS_ZSTD and (args.input.endswith('.zst')):
        print("Error: File has .zst extension but zstandard library not installed.")
        print("Install with: pip install zstandard")
        return None

    # Load Kafka configuration
    kafka_config = None
    if args.kafka_config:
        try:
            kafka_config = load_kafka_config(args.kafka_config)
            print(f"Loaded Kafka configuration from {args.kafka_config}")
        except Exception as e:
            print(f"Error loading config: {e}")
            return None

    return {
        'brokers': args.brokers,
        'preserve_timestamps': args.preserve_timestamps,
        'kafka_config': kafka_config,
        'topic': args.topic,
        'file_format': args.format,
        'rate_limit': args.rate_limit,
        'input_file': args.input,
    }

def make_uploader_and_send(broker, preserve_timestamps, kafka_config, topic, file_format, rate_limit, input_file):

    uploader = KafkaMessageUploader(broker, preserve_timestamps, kafka_config)

    if rate_limit:
        uploader.batch_upload_with_rate_limit(input_file, topic, rate_limit, file_format)
    else:
        if file_format == 'json':
            uploader.upload_from_jsonl(input_file, topic)
        elif file_format == 'binary':
            uploader.upload_from_binary(input_file, topic)


def main():
    params = parse_args(make_parser().parse_args())
    if not isinstance(params, dict) or not len(params):
        print(f"Failed to convert arguments to parameters: {params}", file=sys.stderr)
        return 1
    try:
        make_uploader_and_send(**params)
    except KeyboardInterrupt:
        print("\nUpload interrupted by user")
    except Exception as e:
        print(f"Upload failed: {e}")
        return 1



if __name__ == "__main__":
    main()
