export { ESPLoader, Transport } from "esptool-js";
import SparkMD5 from "spark-md5";
export const md5 = (bytes) =>
  SparkMD5.ArrayBuffer.hash(
    bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength),
  );
