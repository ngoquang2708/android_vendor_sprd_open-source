package com.spreadtrum.android.eng;


public interface engconstents {
    // From the top of engat.h
		int ENG_AT_REQUEST_MODEM_VERSION = 0; 	//get version
		int ENG_AT_REQUEST_IMEI          = 1;	//get imei
		int ENG_AT_SELECT_BAND			 = 2;  	//band select
		int ENG_AT_CURRENT_BAND			 = 3;	//current band
		int ENG_AT_SETARMLOG			 = 4;   //start/stop armlog
		int ENG_AT_GETARMLOG			 = 5;	//get armlog			//5
		int ENG_AT_SETAUTOANSWER		 = 6;	//set call auto answer
		int ENG_AT_GETAUTOANSWER		 = 7;	//get call auto answer status
		int ENG_AT_SETSPPSRATE			 = 8;	//set download/upload rate
		int ENG_AT_GETSPPSRATE           = 9;  //get current rate
		int ENG_AT_SETSPTEST             = 10;	//set sp test			//10
		int ENG_AT_GETSPTEST             = 11;	//get sp test
		int ENG_AT_SPID                  = 12;  //get UE Identity
		int ENG_AT_SETSPFRQ              = 13;  //lock frequnece
		int ENG_AT_GETSPFRQ              = 14; //get frequence
		int ENG_AT_SPAUTE                = 15; //audio loopback test		//15
		int ENG_AT_SETSPDGCNUM           = 16;//set dummy gsm cell
		int ENG_AT_GETSPDGCNUM           = 17;//get dummy gsm cell
		int ENG_AT_SETSPDGCINFO          = 18;//set dunmmy gsm info
		int ENG_AT_GETSPDGCINFO          = 19;//set dunmmy gsm info
		int ENG_AT_GRRTDNCELL            = 20;//set dnummy td ncell info //20
		int ENG_AT_SPL1ITRRSWITCH        = 21;//start/stop L1ITa
		int ENG_AT_GETSPL1ITRRSWITCH     = 22;//get ENG_AT_SPL1ITRRSWITCH status
		int ENG_AT_PCCOTDCELL            = 23;//tdd target cell
		int ENG_AT_SDATACONF             = 24;//data config
		int ENG_AT_L1PARAM               = 25;//set l1param value		//25
		int ENG_AT_TRRPARAM              = 26;//TRR BCFE param
		int ENG_AT_SETTDMEASSWTH         = 27;//set RR TD switch
		int ENG_AT_GETTDMEASSWTH         = 28;//get RR TD switch
		int ENG_AT_RRDMPARAM             = 29;//RRDM param
		int ENG_AT_DMPARAMRESET          = 30;//reset param			//30
		int ENG_AT_SMTIMER               = 31;//set timer
		int ENG_AT_TRRDCFEPARAM          = 32;//TRR DCFE
		int ENG_AT_CIMI                  = 33;//get imsi
		int ENG_AT_MBCELLID              = 34;//CELL ID
		int ENG_AT_MBAU                  = 35;//					//35
		int ENG_AT_EUICC                 = 36;//get usim/sim
		int ENG_AT_CGREG                 = 37;//get lai
		int ENG_AT_EXIT                  = 38;//set eng exit	
		int ENG_AT_NOHANDLE_CMD			 = 39;//no need handle
		int ENG_AT_SYSINFO				 = 40;//get system info
		int ENG_AT_HVER					 = 41;//get hardware version
		int ENG_AT_GETSYSCONFIG			 = 42;
		int ENG_AT_SETSYSCONFIG			 = 43;
		int ENG_AT_SPVER				 = 44;
		int ENG_AT_AUTOATTACH				=45;
		int ENG_AT_SETAUTOATTACH				=46;
		int ENG_AT_PDPACTIVE					=47;
		int ENG_AT_GETPDPACT					=48;
		int ENG_AT_SGPRSDATA					=49;
		int ENG_AT_GETUPLMN					=50;
		int ENG_AT_CGSMS 						=51;
		int ENG_AT_CAOC						=52;//aoc active,and {Deactive see@ENG_AT_CAOCD} {Query see@ENG_AT_CAOCQ}
		int ENG_AT_CAMM						=53;
		int ENG_AT_SETCOPS 					=54;
		int ENG_AT_SADC					 	= 55;
		int ENG_AT_CFUN						= 56;
		int ENG_AT_CGMR						= 57;
		int ENG_AT_SETCAPLOG				= 58;
		int ENG_AT_SETUPLMN 				= 59;
		int ENG_AT_GETUPLMNLEN				= 60;
		int ENG_AT_GETDSPLOG			 = 61;   //
		int ENG_AT_SETDSPLOG			 = 62;   //
		int ENG_AT_SGMR							= 70;
		int ENG_AT_CAOCD					= 80;//aoc deactive
		int ENG_AT_CAOCQ					= 81;//aoc query
		
		int ENG_AT_CCED						= 101;//net info of sim
		int ENG_AT_L1MON					= 103;

		int ENG_AT_GET_ASSERT_MODE          = 108;
		int ENG_AT_SET_ASSERT_MODE          = 109;
		int ENG_AT_SET_MANUAL_ASSERT        = 110;
		int ENG_AT_SFPL                     = 111;
		int ENG_AT_SEPL                     = 112;

		int ENG_AT_SPENGMD_QUERY			= 117;
		int ENG_AT_SPENGMD_OPEN				= 118;
		int ENG_AT_SPENGMD_CLOSE			= 119;
      int ENG_AT_SSMP = 200;
		//ENG_AT_CMD_END,
}
