export function validatePcm(bytes) {
  if (!Buffer.isBuffer(bytes) || bytes.length % 2 || bytes.length > 192000) throw new Error('Invalid PCM frame');
  return bytes;
}

// Aggregate numeric levels only; no PCM reference or sample history is retained.
export class PcmMeter {
  #samples = 0; #energy = 0; #peak = 0;
  push(bytes) {
    validatePcm(bytes);
    for (let i = 0; i < bytes.length; i += 2) {
      const sample = bytes.readInt16LE(i);
      this.#energy += sample * sample; this.#peak = Math.max(this.#peak, Math.abs(sample));
    }
    this.#samples += bytes.length / 2;
  }
  snapshot() {
    if (!this.#samples) return {samples: 0, rmsDb: null, peakDb: null};
    const db = amplitude => Math.round(Math.max(-100, 20 * Math.log10(amplitude / 32768)) * 100) / 100;
    return {samples: this.#samples, rmsDb: db(Math.sqrt(this.#energy / this.#samples)), peakDb: db(this.#peak)};
  }
}

// Streaming linear interpolation; the retained edge sample avoids frame-boundary clicks.
export class PcmResampler {
  constructor(inputRate, outputRate) {
    if (![inputRate, outputRate].every(rate => Number.isInteger(rate) && rate >= 8000 && rate <= 48000)) throw new Error('Unsupported PCM sample rate');
    this.step = inputRate / outputRate; this.samples = []; this.position = 0;
  }
  push(bytes) {
    validatePcm(bytes);
    for (let i = 0; i < bytes.length; i += 2) this.samples.push(bytes.readInt16LE(i));
    return this.convert(false);
  }
  convert(final) {
    const values = [];
    while (this.position < this.samples.length && (final || this.position + 1 < this.samples.length)) {
      const left = Math.floor(this.position), fraction = this.position - left;
      const a = this.samples[left], b = this.samples[Math.min(left + 1, this.samples.length - 1)];
      values.push(Math.round(a + (b - a) * fraction)); this.position += this.step;
    }
    const consumed = Math.min(Math.floor(this.position), this.samples.length);
    this.samples.splice(0, consumed); this.position -= consumed;
    const output = Buffer.alloc(values.length * 2);
    values.forEach((value, i) => output.writeInt16LE(value, i * 2)); return output;
  }
  finish() { const bytes = this.convert(true); this.samples = []; this.position = 0; return bytes; }
}
