/* gbnpacket.c - defines the go-back-n packet structure
 * by Elijah Jordan Montgomery <elijah.montgomery@uky.edu>
 */
struct gbnpacket
{
  int type;
  int seq_no;
  int length;
  char data[512];
};
