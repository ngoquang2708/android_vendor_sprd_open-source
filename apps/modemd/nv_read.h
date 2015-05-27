
/* param in: 
	path ---first nv path like :/dev/block/platform/
	Bak_path--- second nv path
	path_out---cp path loaded the nv

	return value:
	   -1: invalid parameter
	   0: error
	   1:success	
*/
int read_nv_partition(char *path, char *Bak_path, char *path_out);
