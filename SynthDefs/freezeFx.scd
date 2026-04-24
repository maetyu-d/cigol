// Loaded by scripts/build_synthdefs.sh on a machine with working sclang.
~logicLikeSynthDef = SynthDef(\freezeFx, { |inBus = 0, out = 0, freeze = 0.0, mix = 0.6|
    var dry = In.ar(inBus, 2);
    var chain = FFT(LocalBuf(2048), dry);
    chain = PV_MagFreeze(chain, freeze);
    var wet = IFFT(chain);
    Out.ar(out, XFade2.ar(dry, wet, (mix * 2.0) - 1.0));
});
