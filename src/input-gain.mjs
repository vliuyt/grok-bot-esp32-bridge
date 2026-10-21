import {validatePcm} from './audio.mjs';

const MAX_GAIN = 4;
const PEAK = 30000;
const RELEASE = 1 - Math.exp(-1 / (24000 * 0.5));

// Device-only 24 kHz input: raise quiet speech, lower gain immediately on peaks,
// then recover with a 500 ms time constant. Only a numeric envelope is retained.
export class DeviceInputGain {
  #gain = MAX_GAIN;
  push(bytes) {
    validatePcm(bytes);
    const output = Buffer.alloc(bytes.length);
    for (let at = 0; at < bytes.length; at += 2) {
      const sample = bytes.readInt16LE(at);
      // Unity is the floor: already loud input is preserved, not attenuated.
      const safe = sample ? Math.max(1, Math.min(MAX_GAIN, PEAK / Math.abs(sample))) : MAX_GAIN;
      this.#gain = Math.min(safe, this.#gain + (MAX_GAIN - this.#gain) * RELEASE);
      output.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(sample * this.#gain))), at);
    }
    return output;
  }
}
