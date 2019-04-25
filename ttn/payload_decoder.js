function Decoder(bytes, port) {
  var decoded = {};
  scale = 10;
  // 32 bit, latitude
  humidity = bytes[0];
  temperature = ((0x0f & bytes[2]) << 8) |  (0xff & bytes[1]);
  windspeed = ((0xff & bytes[3]) << 4) | ((0xf0 & bytes[2]) >> 4);
  devid = ((0xff & bytes[7]) << 24) | ((0xff & bytes[6]) << 16) | ((0xff & bytes[5]) << 8) | (0xff & bytes[4] << 0);
  // coordinates need to be converted from int32 to float
  decoded['humidity'] = humidity;
  decoded['temperature'] = (temperature - 500) / scale;
  decoded['windspeed'] = windspeed / scale;
  decoded['devid'] = devid;
  return decoded;
}
