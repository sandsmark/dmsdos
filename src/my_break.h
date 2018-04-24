#ifndef _MY_BREAK_H
#define _MY_BREAK_H

#include <linux/sched.h>

#define BREAK_NOW my_break_now(__PRETTY_FUNCTION__);

#define get_esp() ({int a_sp; __asm__ ("mov %%esp,%0":"=r"(a_sp)); a_sp;}) 

struct wait_queue * my_break_wait=NULL;

static void my_break_cont(void)
{  wake_up(&my_break_wait);
}

static void my_break_now(char * s)
{ unsigned call_pc;
  unsigned call_sp;
  call_pc=(typeof(call_pc))__builtin_return_address(0);
  call_sp=(typeof(call_sp))get_esp();
  printk ("BREAK : procces %i (current=0x%x) stopped at 0x%x in %s\n",
	current->pid,(unsigned)current,call_pc,s);
  printk ("BREAK : sp = 0x%x\n",call_sp);
  printk ("BREAK : for continue call *0x%x()\n",
	(unsigned)&my_break_cont);
  sleep_on(&my_break_wait);
  printk ("BREAK : continuing\n");
}

#endif /* _MY_BREAK_H */
