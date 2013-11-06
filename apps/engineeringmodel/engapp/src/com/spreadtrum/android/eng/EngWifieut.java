package com.spreadtrum.android.eng;


public class EngWifieut {

	static{
		System.loadLibrary("engmodeljni");
	}
	
	private native int ptestCw(PtestCw cw);
	private native int ptestTx(PtestTx cw);
	private native int ptestRx(PtestRx rx);
	private native int ptestInit();
	private native int ptestDeinit();
	private native void ptestSetValue();
	private native int ptestBtStart();
	private native int ptestBtStop();
	
	public int testCw(PtestCw cw){
		return ptestCw(cw);
	}
	
	public int testTx(PtestTx tx){
		return ptestTx(tx);
	}
	
	public int testRx(PtestRx rx){
		return ptestRx(rx);
	}

	public int testInit(){
		return ptestInit();
	}
	public int testDeinit(){
		return ptestDeinit();
	}
	public void testSetValue(int val){
		ptestSetValue();
	}
	public int testBtStart(){
		return ptestBtStart();
	}
	public int testBtStop(){
		return ptestBtStop();
	}
	
	static class PtestCw{
		public int band;
		public int channel;
		public int sFactor;
		public int frequency; 
		public int frequencyOffset;
		public int amplitude;
	}
	static class PtestTx{
		public int band;
		public int channel;
		public int sFactor;
		public int rate;
		public int powerLevel;
		public int length;
		public int enablelbCsTestMode;//boolean
		public int interval;
		public String destMacAddr;
		public int preamble;
	}
	static class PtestRx{
		public int band;
		public int channel;
		public int sFactor;
		public int frequency;
		public int filteringEnable;//boolean
	}

}
