#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: ln old new\n");
    exit(1);
  }
  if(link(argv[1], argv[2]) < 0) // ex: ln /home/root / 這樣會把 /home/root 連到根目錄
    fprintf(2, "link %s %s: failed\n", argv[1], argv[2]);
  exit(0);
}