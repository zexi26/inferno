# warning: autogenerated code; don't bother to change this, change mktypeset.b or fs.b instead
Fstypes: module {
	PATH: con "/dis/alphabet/fstypes.dis";
	Fscvt: adt {
		values: ref Extvalues->Values[ref Fs->Value];

		int2ext: fn(cvt: self ref Fscvt, v: ref Fs->Value): ref Alphabet->Value;
		ext2int: fn(cvt: self ref Fscvt, ev: ref Alphabet->Value): ref Fs->Value;
		dup: fn(cvt: self ref Fscvt, ev: ref Alphabet->Value): ref Alphabet->Value;
		free: fn(cvt: self ref Fscvt, ev: ref Alphabet->Value, used: int);
	};

	proxy: fn(): chan of ref Proxy->Typescmd[ref Alphabet->Value];
	proxy0: fn(): (
			chan of ref Proxy->Typescmd[ref Alphabet->Value],
			chan of (string, chan of ref Proxy->Typescmd[ref Fs->Value]),
			ref Fscvt
		);
};

Fssubtypes: module {
	proxy: fn(): chan of ref Proxy->Typescmd[ref Fs->Value];
};