/*
 *  Driver core for serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/serial.h> /* for serial_state and serial_icounter_struct */
#include <linux/serial_core.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#include <asm/irq.h>
#include <asm/uaccess.h>

/*
 * This is used to lock changes in serial line configuration.
 */
static DEFINE_MUTEX(port_mutex);

/*
 * lockdep: port->lock is initialized in two places, but we
 *          want only one lock-class:
 */
static struct lock_class_key port_lock_key;

#define HIGH_BITS_OFFSET	((sizeof(long)-sizeof(int))*8)

static void uart_change_speed(struct tty_struct *tty, struct uart_state *state,
					struct ktermios *old_termios);
static void uart_wait_until_sent(struct tty_struct *tty, int timeout);
static void uart_change_pm(struct uart_state *state,
			   enum uart_pm_state pm_state);

static void uart_port_shutdown(struct tty_port *port);

static int uart_dcd_enabled(struct uart_port *uport)
{
	return uport->status & UPSTAT_DCD_ENABLE;
}

/*
 * This routine is used by the interrupt handler to schedule processing in
 * the software interrupt portion of the driver.
 */
void uart_write_wakeup(struct uart_port *port)
{
	struct uart_state *state = port->state;
	/*
	 * This means you called this function _after_ the port was
	 * closed.  No cookie for you.
	 */
	BUG_ON(!state);
	tty_wakeup(state->port.tty);
}

static void uart_stop(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port = state->uart_port;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	port->ops->stop_tx(port);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void __uart_start(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port = state->uart_port;

	if (port->ops->wake_peer)
		port->ops->wake_peer(port);

	if (!uart_tx_stopped(port))
		port->ops->start_tx(port);
}

static void uart_start(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port = state->uart_port;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	__uart_start(tty);
	spin_unlock_irqrestore(&port->lock, flags);
}

static inline void
uart_update_mctrl(struct uart_port *port, unsigned int set, unsigned int clear)
{
	unsigned long flags;
	unsigned int old;

	spin_lock_irqsave(&port->lock, flags);
	old = port->mctrl;
	port->mctrl = (old & ~clear) | set;
	if (old != port->mctrl)
		port->ops->set_mctrl(port, port->mctrl);
	spin_unlock_irqrestore(&port->lock, flags);
}

#define uart_set_mctrl(port, set)	uart_update_mctrl(port, set, 0)
#define uart_clear_mctrl(port, clear)	uart_update_mctrl(port, 0, clear)

/*
 * Startup the port.  This will be called once per open.  All calls
 * will be serialised by the per-port mutex.
 */
static int uart_port_startup(struct tty_struct *tty, struct uart_state *state,
		int init_hw)
{
	struct uart_port *uport = state->uart_port;
	unsigned long page;
	int retval = 0;

	if (uport->type == PORT_UNKNOWN)
		return 1;

	/*
	 * Make sure the device is in D0 state.
	 */
	uart_change_pm(state, UART_PM_STATE_ON);

	/*
	 * Initialise and allocate the transmit and temporary
	 * buffer.
	 */
	if (!state->xmit.buf) {
		/* This is protected by the per port mutex */
		page = get_zeroed_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;

		state->xmit.buf = (unsigned char *) page;
		uart_circ_clear(&state->xmit);
	}

	retval = uport->ops->startup(uport);
	if (retval == 0) {
		if (uart_console(uport) && uport->cons->cflag) {
			tty->termios.c_cflag = uport->cons->cflag;
			uport->cons->cflag = 0;
		}
		/*
		 * Initialise the hardware port settings.
		 */
		uart_change_speed(tty, state, NULL);

		if (init_hw) {
			/*
			 * Setup the RTS and DTR signals once the
			 * port is open and ready to respond.
			 */
			if (tty->termios.c_cflag & CBAUD)
				uart_set_mctrl(uport, TIOCM_RTS | TIOCM_DTR);
		}

		spin_lock_irq(&uport->lock);
		if (uart_cts_enabled(uport) &&
		    !(uport->ops->get_mctrl(uport) & TIOCM_CTS))
			uport->hw_stopped = 1;
		else
			uport->hw_stopped = 0;
		spin_unlock_irq(&uport->lock);
	}

	/*
	 * This is to allow setserial on this port. People may want to set
	 * port/irq/type and then reconfigure the port properly if it failed
	 * now.
	 */
	if (retval && capable(CAP_SYS_ADMIN))
		return 1;

	return retval;
}

static int uart_startup(struct tty_struct *tty, struct uart_state *state,
		int init_hw)
{
	struct tty_port *port = &state->port;
	int retval;

	if (port->flags & ASYNC_INITIALIZED)
		return 0;

	/*
	 * Set the TTY IO error marker - we will only clear this
	 * once we have successfully opened the port.
	 */
	set_bit(TTY_IO_ERROR, &tty->flags);

	retval = uart_port_startup(tty, state, init_hw);
	if (!retval) {
		set_bit(ASYNCB_INITIALIZED, &port->flags);
		clear_bit(TTY_IO_ERROR, &tty->flags);
	} else if (retval > 0)
		retval = 0;

	return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.  Calls to
 * uart_shutdown are serialised by the per-port semaphore.
 */
static void uart_shutdown(struct tty_struct *tty, struct uart_state *state)
{
	struct uart_port *uport = state->uart_port;
	struct tty_port *port = &state->port;

	/*
	 * Set the TTY IO error marker
	 */
	if (tty)
		set_bit(TTY_IO_ERROR, &tty->flags);

	if (test_and_clear_bit(ASYNCB_INITIALIZED, &port->flags)) {
		/*
		 * Turn off DTR and RTS early.
		 */
		if (uart_console(uport) && tty)
			uport->cons->cflag = tty->termios.c_cflag;

		if (!tty || (tty->termios.c_cflag & HUPCL))
			uart_clear_mctrl(uport, TIOCM_DTR | TIOCM_RTS);

		uart_port_shutdown(port);
	}

	/*
	 * It's possible for shutdown to be called after suspend if we get
	 * a DCD drop (hangup) at just the right time.  Clear suspended bit so
	 * we don't try to resume a port that has been shutdown.
	 */
	clear_bit(ASYNCB_SUSPENDED, &port->flags);

	/*
	 * Free the transmit buffer page.
	 */
	if (state->xmit.buf) {
		free_page((unsigned long)state->xmit.buf);
		state->xmit.buf = NULL;
	}
}

/**
 *	uart_update_timeout - update per-port FIFO timeout.
 *	@port:  uart_port structure describing the port
 *	@cflag: termios cflag value
 *	@baud:  speed of the port
 *
 *	Set the port FIFO timeout value.  The @cflag value should
 *	reflect the actual hardware settings.
 */
void
uart_update_timeout(struct uart_port *port, unsigned int cflag,
		    unsigned int baud)
{
	unsigned int bits;

	/* byte size and parity */
	switch (cflag & CSIZE) {
	case CS5:
		bits = 7;
		break;
	case CS6:
		bits = 8;
		break;
	case CS7:
		bits = 9;
		break;
	default:
		bits = 10;
		break; /* CS8 */
	}

	if (cflag & CSTOPB)
		bits++;
	if (cflag & PARENB)
		bits++;

	/*
	 * The total number of bits to be transmitted in the fifo.
	 */
	bits = bits * port->fifosize;

	/*
	 * Figure the timeout to send the above number of bits.
	 * Add .02 seconds of slop
	 */
	port->timeout = (HZ * bits) / baud + HZ/50;
}

EXPORT_SYMBOL(uart_update_timeout);

/**
 *	uart_get_baud_rate - return baud rate for a particular port
 *	@port: uart_port structure describing the port in question.
 *	@termios: desired termios settings.
 *	@old: old termios (or NULL)
 *	@min: minimum acceptable baud rate
 *	@max: maximum acceptable baud rate
 *
 *	Decode the termios structure into a numeric baud rate,
 *	taking account of the magic 38400 baud rate (with spd_*
 *	flags), and mapping the %B0 rate to 9600 baud.
 *
 *	If the new baud rate is invalid, try the old termios setting.
 *	If it's still invalid, we try 9600 baud.
 *
 *	Update the @termios structure to reflect the baud rate
 *	we're actually going to be using. Don't do this for the case
 *	where B0 is requested ("hang up").
 */
unsigned int
uart_get_baud_rate(struct uart_port *port, struct ktermios *termios,
		   struct ktermios *old, unsigned int min, unsigned int max)
{
	unsigned int try, baud, altbaud = 38400;
	int hung_up = 0;
	upf_t flags = port->flags & UPF_SPD_MASK;

	if (flags == UPF_SPD_HI)
		altbaud = 57600;
	else if (flags == UPF_SPD_VHI)
		altbaud = 115200;
	else if (flags == UPF_SPD_SHI)
		altbaud = 230400;
	else if (flags == UPF_SPD_WARP)
		altbaud = 460800;

	for (try = 0; try < 2; try++) {
		baud = tty_termios_baud_rate(termios);

		/*
		 * The spd_hi, spd_vhi, spd_shi, spd_warp kludge...
		 * Die! Die! Die!
		 */
		if (try == 0 && baud == 38400)
			baud = altbaud;

		/*
		 * Special case: B0 rate.
		 */
		if (baud == 0) {
			hung_up = 1;
			baud = 9600;
		}

		if (baud >= min && baud <= max)
			return baud;

		/*
		 * Oops, the quotient was zero.  Try again with
		 * the old baud rate if possible.
		 */
		termios->c_cflag &= ~CBAUD;
		if (old) {
			baud = tty_termios_baud_rate(old);
			if (!hung_up)
				tty_termios_encode_baud_rate(termios,
								baud, baud);
			old = NULL;
			continue;
		}

		/*
		 * As a last resort, if the range cannot be met then clip to
		 * the nearest chip supported rate.
		 */
		if (!hung_up) {
			if (baud <= min)
				tty_termios_encode_baud_rate(termios,
							min + 1, min + 1);
			else
				tty_termios_encode_baud_rate(termios,
							max - 1, max - 1);
		}
	}
	/* Should never happen */
	WARN_ON(1);
	return 0;
}

EXPORT_SYMBOL(uart_get_baud_rate);

/**
 *	uart_get_divisor - return uart clock divisor
 *	@port: uart_port structure describing the port.
 *	@baud: desired baud rate
 *
 *	Calculate the uart clock divisor for the port.
 */
unsigned int
uart_get_divisor(struct uart_port *port, unsigned int baud)
{
	unsigned int quot;

	/*
	 * Old custom speed handling.
	 */
	if (baud == 38400 && (port->flags & UPF_SPD_MASK) == UPF_SPD_CUST)
		quot = port->custom_divisor;
	else
		quot = DIV_ROUND_CLOSEST(port->uartclk, 16 * baud);

	return quot;
}

EXPORT_SYMBOL(uart_get_divisor);

/* FIXME: Consistent locking policy */
static void uart_change_speed(struct tty_struct *tty, struct uart_state *state,
					struct ktermios *old_termios)
{
	struct uart_port *uport = state->uart_port;
	struct ktermios *termios;

	/*
	 * If we have no tty, termios, or the port does not exist,
	 * then we can't set the parameters for this port.
	 */
	if (!tty || uport->type == PORT_UNKNOWN)
		return;

	termios = &tty->termios;
	uport->ops->set_termios(uport, termios, old_termios);

	/*
	 * Set modem status enables based on termios cflag
	 */
	spin_lock_irq(&uport->lock);
	if (termios->c_cflag & CRTSCTS)
		uport->status |= UPSTAT_CTS_ENABLE;
	else
		uport->status &= ~UPSTAT_CTS_ENABLE;

	if (termios->c_cflag & CLOCAL)
		uport->status &= ~UPSTAT_DCD_ENABLE;
	else
		uport->status |= UPSTAT_DCD_ENABLE;
	spin_unlock_irq(&uport->lock);
}

static inline int __uart_put_char(struct uart_port *port,
				struct circ_buf *circ, unsigned char c)
{
	unsigned long flags;
	int ret = 0;

	if (!circ->buf)
		return 0;

	spin_lock_irqsave(&port->lock, flags);
	if (uart_circ_chars_free(circ) != 0) {
		circ->buf[circ->head] = c;
		circ->head = (circ->head + 1) & (UART_XMIT_SIZE - 1);
		ret = 1;
	}
	spin_unlock_irqrestore(&port->lock, flags);
	return ret;
}

static int uart_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct uart_state *state = tty->driver_data;

	return __uart_put_char(state->uart_port, &state->xmit, ch);
}

static void uart_flush_chars(struct tty_struct *tty)
{
	uart_start(tty);
}

static int uart_write(struct tty_struct *tty,
					const unsigned char *buf, int count)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	struct circ_buf *circ;
	unsigned long flags;
	int c, ret = 0;

	/*
	 * This means you called this function _after_ the port was
	 * closed.  No cookie for you.
	 */
	if (!state) {
		WARN_ON(1);
		return -EL3HLT;
	}

	port = state->uart_port;
	circ = &state->xmit;

	if (!circ->buf)
		return 0;

	spin_lock_irqsave(&port->lock, flags);
	while (1) {
		c = CIRC_SPACE_TO_END(circ->head, circ->tail, UART_XMIT_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
		memcpy(circ->buf + circ->head, buf, c);
		circ->head = (circ->head + c) & (UART_XMIT_SIZE - 1);
		buf += c;
		count -= c;
		ret += c;
	}
	spin_unlock_irqrestore(&port->lock, flags);

	uart_start(tty);
	return ret;
}

static int uart_write_room(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&state->uart_port->lock, flags);
	ret = uart_circ_chars_free(&state->xmit);
	spin_unlock_irqrestore(&state->uart_port->lock, flags);
	return ret;
}

static int uart_chars_in_buffer(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&state->uart_port->lock, flags);
	ret = uart_circ_chars_pending(&state->xmit);
	spin_unlock_irqrestore(&state->uart_port->lock, flags);
	return ret;
}

static void uart_flush_buffer(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port;
	unsigned long flags;

	/*
	 * This means you called this function _after_ the port was
	 * closed.  No cookie for you.
	 */
	if (!state) {
		WARN_ON(1);
		return;
	}

	port = state->uart_port;
	pr_debug("uart_flush_buffer(%d) called\n", tty->index);

	spin_lock_irqsave(&port->lock, flags);
	uart_circ_clear(&state->xmit);
	if (port->ops->flush_buffer)
		port->ops->flush_buffer(port);
	spin_unlock_irqrestore(&port->lock, flags);
	tty_wakeup(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void uart_send_xchar(struct tty_struct *tty, char ch)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port = state->uart_port;
	unsigned long flags;

	if (port->ops->send_xchar)
		port->ops->send_xchar(port, ch);
	else {
		spin_lock_irqsave(&port->lock, flags);
		port->x_char = ch;
		if (ch)
			port->ops->start_tx(port);
		spin_unlock_irqrestore(&port->lock, flags);
	}
}

static void uart_throttle(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port = state->uart_port;
	uint32_t mask = 0;

	if (I_IXOFF(tty))
		mask |= UPF_SOFT_FLOW;
	if (tty->termios.c_cflag & CRTSCTS)
		mask |= UPF_HARD_FLOW;

	if (port->flags & mask) {
		port->ops->throttle(port);
		mask &= ~port->flags;
	}

	if (mask & UPF_SOFT_FLOW)
		uart_send_xchar(tty, STOP_CHAR(tty));

	if (mask & UPF_HARD_FLOW)
		uart_clear_mctrl(port, TIOCM_RTS);
}

static void uart_unthrottle(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port = state->uart_port;
	uint32_t mask = 0;

	if (I_IXOFF(tty))
		mask |= UPF_SOFT_FLOW;
	if (tty->termios.c_cflag & CRTSCTS)
		mask |= UPF_HARD_FLOW;

	if (port->flags & mask) {
		port->ops->unthrottle(port);
		mask &= ~port->flags;
	}

	if (mask & UPF_SOFT_FLOW)
		uart_send_xchar(tty, START_CHAR(tty));

	if (mask & UPF_HARD_FLOW)
		uart_set_mctrl(port, TIOCM_RTS);
}

static void do_uart_get_info(struct tty_port *port,
			struct serial_struct *retinfo)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport = state->uart_port;

	memset(retinfo, 0, sizeof(*retinfo));

	retinfo->type	    = uport->type;
	retinfo->line	    = uport->line;
	retinfo->port	    = uport->iobase;
	if (HIGH_BITS_OFFSET)
		retinfo->port_high = (long) uport->iobase >> HIGH_BITS_OFFSET;
	retinfo->irq		    = uport->irq;
	retinfo->flags	    = uport->flags;
	retinfo->xmit_fifo_size  = uport->fifosize;
	retinfo->baud_base	    = uport->uartclk / 16;
	retinfo->close_delay	    = jiffies_to_msecs(port->close_delay) / 10;
	retinfo->closing_wait    = port->closing_wait == ASYNC_CLOSING_WAIT_NONE ?
				ASYNC_CLOSING_WAIT_NONE :
				jiffies_to_msecs(port->closing_wait) / 10;
	retinfo->custom_divisor  = uport->custom_divisor;
	retinfo->hub6	    = uport->hub6;
	retinfo->io_type         = uport->iotype;
	retinfo->iomem_reg_shift = uport->regshift;
	retinfo->iomem_base      = (void *)(unsigned long)uport->mapbase;
}

static void uart_get_info(struct tty_port *port,
			struct serial_struct *retinfo)
{
	/* Ensure the state we copy is consistent and no hardware changes
	   occur as we go */
	mutex_lock(&port->mutex);
	do_uart_get_info(port, retinfo);
	mutex_unlock(&port->mutex);
}

static int uart_get_info_user(struct tty_port *port,
			 struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;
	uart_get_info(port, &tmp);

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int uart_set_info(struct tty_struct *tty, struct tty_port *port,
			 struct uart_state *state,
			 struct serial_struct *new_info)
{
	struct uart_port *uport = state->uart_port;
	unsigned long new_port;
	unsigned int change_irq, change_port, closing_wait;
	unsigned int old_custom_divisor, close_delay;
	upf_t old_flags, new_flags;
	int retval = 0;

	new_port = new_info->port;
	if (HIGH_BITS_OFFSET)
		new_port += (unsigned long) new_info->port_high << HIGH_BITS_OFFSET;

	new_info->irq = irq_canonicalize(new_info->irq);
	close_delay = msecs_to_jiffies(new_info->close_delay * 10);
	closing_wait = new_info->closing_wait == ASYNC_CLOSING_WAIT_NONE ?
			ASYNC_CLOSING_WAIT_NONE :
			msecs_to_jiffies(new_info->closing_wait * 10);


	change_irq  = !(uport->flags & UPF_FIXED_PORT)
		&& new_info->irq != uport->irq;

	/*
	 * Since changing the 'type' of the port changes its resource
	 * allocations, we should treat type changes the same as
	 * IO port changes.
	 */
	change_port = !(uport->flags & UPF_FIXED_PORT)
		&& (new_port != uport->iobase ||
		    (unsigned long)new_info->iomem_base != uport->mapbase ||
		    new_info->hub6 != uport->hub6 ||
		    new_info->io_type != uport->iotype ||
		    new_info->iomem_reg_shift != uport->regshift ||
		    new_info->type != uport->type);

	old_flags = uport->flags;
	new_flags = new_info->flags;
	old_custom_divisor = uport->custom_divisor;

	if (!capable(CAP_SYS_ADMIN)) {
		retval = -EPERM;
		if (change_irq || change_port ||
		    (new_info->baud_base != uport->uartclk / 16) ||
		    (close_delay != port->close_delay) ||
		    (closing_wait != port->closing_wait) ||
		    (new_info->xmit_fifo_size &&
		     new_info->xmit_fifo_size != uport->fifosize) ||
		    (((new_flags ^ old_flags) & ~UPF_USR_MASK) != 0))
			goto exit;
		uport->flags = ((uport->flags & ~UPF_USR_MASK) |
			       (new_flags & UPF_USR_MASK));
		uport->custom_divisor = new_info->custom_divisor;
		goto check_and_exit;
	}

	/*
	 * Ask the low level driver to verify the settings.
	 */
	if (uport->ops->verify_port)
		retval = uport->ops->verify_port(uport, new_info);

	if ((new_info->irq >= nr_irqs) || (new_info->irq < 0) ||
	    (new_info->baud_base < 9600))
		retval = -EINVAL;

	if (retval)
		goto exit;

	if (change_port || change_irq) {
		retval = -EBUSY;

		/*
		 * Make sure that we are the sole user of this port.
		 */
		if (tty_port_users(port) > 1)
			goto exit;

		/*
		 * We need to shutdown the serial port at the old
		 * port/type/irq combination.
		 */
		uart_shutdown(tty, state);
	}

	if (change_port) {
		unsigned long old_iobase, old_mapbase;
		unsigned int old_type, old_iotype, old_hub6, old_shift;

		old_iobase = uport->iobase;
		old_mapbase = uport->mapbase;
		old_type = uport->type;
		old_hub6 = uport->hub6;
		old_iotype = uport->iotype;
		old_shift = uport->regshift;

		/*
		 * Free and release old regions
		 */
		if (old_type != PORT_UNKNOWN)
			uport->ops->release_port(uport);

		uport->iobase = new_port;
		uport->type = new_info->type;
		uport->hub6 = new_info->hub6;
		uport->iotype = new_info->io_type;
		uport->regshift = new_info->iomem_reg_shift;
		uport->mapbase = (unsigned long)new_info->iomem_base;

		/*
		 * Claim and map the new regions
		 */
		if (uport->type != PORT_UNKNOWN) {
			retval = uport->ops->request_port(uport);
		} else {
			/* Always success - Jean II */
			retval = 0;
		}

		/*
		 * If we fail to request resources for the
		 * new port, try to restore the old settings.
		 */
		if (retval) {
			uport->iobase = old_iobase;
			uport->type = old_type;
			uport->hub6 = old_hub6;
			uport->iotype = old_iotype;
			uport->regshift = old_shift;
			uport->mapbase = old_mapbase;

			if (old_type != PORT_UNKNOWN) {
				retval = uport->ops->request_port(uport);
				/*
				 * If we failed to restore the old settings,
				 * we fail like this.
				 */
				if (retval)
					uport->type = PORT_UNKNOWN;

				/*
				 * We failed anyway.
				 */
				retval = -EBUSY;
			}

			/* Added to return the correct error -Ram Gupta */
			goto exit;
		}
	}

	if (change_irq)
		uport->irq      = new_info->irq;
	if (!(uport->flags & UPF_FIXED_PORT))
		uport->uartclk  = new_info->baud_base * 16;
	uport->flags            = (uport->flags & ~UPF_CHANGE_MASK) |
				 (new_flags & UPF_CHANGE_MASK);
	uport->custom_divisor   = new_info->custom_divisor;
	port->close_delay     = close_delay;
	port->closing_wait    = closing_wait;
	if (new_info->xmit_fifo_size)
		uport->fifosize = new_info->xmit_fifo_size;
	port->low_latency = (uport->flags & UPF_LOW_LATENCY) ? 1 : 0;

 check_and_exit:
	retval = 0;
	if (uport->type == PORT_UNKNOWN)
		goto exit;
	if (port->flags & ASYNC_INITIALIZED) {
		if (((old_flags ^ uport->flags) & UPF_SPD_MASK) ||
		    old_custom_divisor != uport->custom_divisor) {
			/*
			 * If they're setting up a custom divisor or speed,
			 * instead of clearing it, then bitch about it. No
			 * need to rate-limit; it's CAP_SYS_ADMIN only.
			 */
			if (uport->flags & UPF_SPD_MASK) {
				char buf[64];

				dev_notice(uport->dev,
				       "%s sets custom speed on %s. This is deprecated.\n",
				      current->comm,
				      tty_name(port->tty, buf));
			}
			uart_change_speed(tty, state, NULL);
		}
	} else
		retval = uart_startup(tty, state, 1);
 exit:
	return retval;
}

static int uart_set_info_user(struct tty_struct *tty, struct uart_state *state,
			 struct serial_struct __user *newinfo)
{
	struct serial_struct new_serial;
	struct tty_port *port = &state->port;
	int retval;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	/*
	 * This semaphore protects port->count.  It is also
	 * very useful to prevent opens.  Also, take the
	 * port configuration semaphore to make sure that a
	 * module insertion/removal doesn't change anything
	 * under us.
	 */
	mutex_lock(&port->mutex);
	retval = uart_set_info(tty, port, state, &new_serial);
	mutex_unlock(&port->mutex);
	return retval;
}

/**
 *	uart_get_lsr_info	-	get line status register info
 *	@tty: tty associated with the UART
 *	@state: UART being queried
 *	@value: returned modem value
 *
 *	Note: uart_ioctl protects us against hangups.
 */
static int uart_get_lsr_info(struct tty_struct *tty,
			struct uart_state *state, unsigned int __user *value)
{
	struct uart_port *uport = state->uart_port;
	unsigned int result;

	result = uport->ops->tx_empty(uport);

	/*
	 * If we're about to load something into the transmit
	 * register, we'll pretend the transmitter isn't empty to
	 * avoid a race condition (depending on when the transmit
	 * interrupt happens).
	 */
	if (uport->x_char ||
	    ((uart_circ_chars_pending(&state->xmit) > 0) &&
	     !uart_tx_stopped(uport)))
		result &= ~TIOCSER_TEMT;

	return put_user(result, value);
}

static int uart_tiocmget(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port = &state->port;
	struct uart_port *uport = state->uart_port;
	int result = -EIO;

	mutex_lock(&port->mutex);
	if (!(tty->flags & (1 << TTY_IO_ERROR))) {
		result = uport->mctrl;
		spin_lock_irq(&uport->lock);
		result |= uport->ops->get_mctrl(uport);
		spin_unlock_irq(&uport->lock);
	}
	mutex_unlock(&port->mutex);

	return result;
}

static int
uart_tiocmset(struct tty_struct *tty, unsigned int set, unsigned int clear)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *uport = state->uart_port;
	struct tty_port *port = &state->port;
	int ret = -EIO;

	mutex_lock(&port->mutex);
	if (!(tty->flags & (1 << TTY_IO_ERROR))) {
		uart_update_mctrl(uport, set, clear);
		ret = 0;
	}
	mutex_unlock(&port->mutex);
	return ret;
}

static int uart_break_ctl(struct tty_struct *tty, int break_state)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port = &state->port;
	struct uart_port *uport = state->uart_port;

	mutex_lock(&port->mutex);

	if (uport->type != PORT_UNKNOWN)
		uport->ops->break_ctl(uport, break_state);

	mutex_unlock(&port->mutex);
	return 0;
}

static int uart_do_autoconfig(struct tty_struct *tty,struct uart_state *state)
{
	struct uart_port *uport = state->uart_port;
	struct tty_port *port = &state->port;
	int flags, ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * Take the per-port semaphore.  This prevents count from
	 * changing, and hence any extra opens of the port while
	 * we're auto-configuring.
	 */
	if (mutex_lock_interruptible(&port->mutex))
		return -ERESTARTSYS;

	ret = -EBUSY;
	if (tty_port_users(port) == 1) {
		uart_shutdown(tty, state);

		/*
		 * If we already have a port type configured,
		 * we must release its resources.
		 */
		if (uport->type != PORT_UNKNOWN)
			uport->ops->release_port(uport);

		flags = UART_CONFIG_TYPE;
		if (uport->flags & UPF_AUTO_IRQ)
			flags |= UART_CONFIG_IRQ;

		/*
		 * This will claim the ports resources if
		 * a port is found.
		 */
		uport->ops->config_port(uport, flags);

		ret = uart_startup(tty, state, 1);
	}
	mutex_unlock(&port->mutex);
	return ret;
}

static void uart_enable_ms(struct uart_port *uport)
{
	/*
	 * Force modem status interrupts on
	 */
	if (uport->ops->enable_ms)
		uport->ops->enable_ms(uport);
}

/*
 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
 * - mask passed in arg for lines of interest
 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
 * Caller should use TIOCGICOUNT to see which one it was
 *
 * FIXME: This wants extracting into a common all driver implementation
 * of TIOCMWAIT using tty_port.
 */
static int
uart_wait_modem_status(struct uart_state *state, unsigned long arg)
{
	struct uart_port *uport = state->uart_port;
	struct tty_port *port = &state->port;
	DECLARE_WAITQUEUE(wait, current);
	struct uart_icount cprev, cnow;
	int ret;

	/*
	 * note the counters on entry
	 */
	spin_lock_irq(&uport->lock);
	memcpy(&cprev, &uport->icount, sizeof(struct uart_icount));
	uart_enable_ms(uport);
	spin_unlock_irq(&uport->lock);

	add_wait_queue(&port->delta_msr_wait, &wait);
	for (;;) {
		spin_lock_irq(&uport->lock);
		memcpy(&cnow, &uport->icount, sizeof(struct uart_icount));
		spin_unlock_irq(&uport->lock);

		set_current_state(TASK_INTERRUPTIBLE);

		if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
		    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
		    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
		    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
			ret = 0;
			break;
		}

		schedule();

		/* see if a signal did it */
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		cprev = cnow;
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&port->delta_msr_wait, &wait);

	return ret;
}

/*
 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
 * Return: write counters to the user passed counter struct
 * NB: both 1->0 and 0->1 transitions are counted except for
 *     RI where only 0->1 is counted.
 */
static int uart_get_icount(struct tty_struct *tty,
			  struct serial_icounter_struct *icount)
{
	struct uart_state *state = tty->driver_data;
	struct uart_icount cnow;
	struct uart_port *uport = state->uart_port;

	spin_lock_irq(&uport->lock);
	memcpy(&cnow, &uport->icount, sizeof(struct uart_icount));
	spin_unlock_irq(&uport->lock);

	icount->cts         = cnow.cts;
	icount->dsr         = cnow.dsr;
	icount->rng         = cnow.rng;
	icount->dcd         = cnow.dcd;
	icount->rx _lock(&port->mute struc ize and paritS&port->mute f set ize and paritf setport->mute NNIl lnze and paritNNIl lnport->mute e size ze and parite sizeport->mute brkuc ize and paritbrkport->mute buf_NNIl lnzd paritbuf_NNIl lnait, &wait)0	return retCmeans vi	/*ys	Note:t sW
	 * ller rt_port;

	spi)  *  ng tty_port.
 */
statiNote:rt_tiocmset(struct tty_struct *tty, uncme co  ate *state, unsigned long arg)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port = &sta
}

statned iu||
	_base   tatned i)||
state->port;
	iNO  (TLCMD_wait	bits++;

 musote:st so
	 poryw setsitialise thally gk;
waited the poarity */mdlag & CSIZshoulSERIAL:t, flags);

		c int uart_getnfig_pu||
IT_SIZRTSYS; & CSIZshouSSERIAL:t, flags);

		c int uart_setrt_startup(tu||
IT_SIZRTSYS; & CSIZshouSERgs |= :t, flags);

		c int uart_do_t_shutdown(ttyIZRTSYS; & CSIZshouSERGWILD:0;
	obwe act u/ & CSIZshouSERSWILD:0;
	obwe act u/ &cts))) {
			ARTSYS;
exit;
		ts))!;
	iNO  (TLCMDORT_UNKNOout flags, x);
	if (!(tty->flags & (1 << TTYchange_it;
	int rT_UNKNOout frt->lock);
	}
.  Nhis IOCM
 * Cal  RI at it dependiialise thaphorewaited the poarity */mdlag & CSIZshouMI;
	D:t, flags);

		c int
uart_wait_modetup(t||
IT_SIZRTSYS;
exit;
		ts))!;
	iNO  (TLCMDORT_UNKNOout flart_port;

	mutex_lock(&port->mutx);
	if (!(tty->flags & (1 << TTYchange_it;
	int rT_UNKNOout_up frt->lock);
	per t

 muporyw seialise thtate: orewait consi No
			bso, tak* This isrotects ng ontyhtate:  */
 uped the poarity */mdlag & CSIZshouSERGETLSR:0;
	Gsr_info	-	get line statuu/ &cts))) static int uart_gert_startup(tu||
IT_SIZRTSYS; &
		break;
	forg arg)
{
	struct uart_port *uport = state->uts on
	 */
	if (usote:)current)) 
	 */
	if (usote:config_pcme t||
IT_SIZRTSYS;
ex
exout_up:ate, 1);
	}
	mutex_unlock(&porout:rt->mutex);
	return ret;
}

staticc inldisc_unthrottle(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *uport = state->uas on
	 */
	if (uc inldisc->enable_ms)
		uc inldisc_et_termiLOW;
	if (tty-info, TIOCM_RTS);
}

staticport->ops->suart_write(struct tty_struct state,
					struct ktermios *old_termios)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *uport = state->uort *port, unsigne ons->cflag = tty->termiouort *port, uniterm_
	uint3IGNBRK|BRKsta|IGNPAR|PARMRK|INPCKioubool sw		}
					cifalsrt->fifosize): i/*st sIOCM
in
 * thf * Ae *srol nt. si No
			kari't emp If wtreat typo t

 mucounter rify the settings.
	 */
	if (uport->flask & UPFNITIALIerm_
	uin|t3IXANY|IXON|0;

	rt_icw		}
					cev.dcds->cflag = tty->c[V	ret ])!;
 ktermios *if (tc[V	ret ])prev.dcds->cflag = tty->c[V	rOP])!;
 ktermios *if (tc[V	rOP] frt->lock);
	}
.ssure that  numbemake refunction is/*
	v siou the sif (upransmit * Ask the low l.sW
	 * l *pled to rBfon't em numbrany->terminy-[io]ets cu
		 *ae {
		ls
 *nd if wpe thr: ttRI ay port->ops->s)pranste(Note:tce settings.its++;
^
 ktermios *if (trt->co
		if (it) > s->cflag = tty-oets cu=;
 ktermios *if (oets cu (it) > s->cflag = tty-iets cu=;
 ktermios *if (iets cu (it) > (_FLOW;
	if (tty-is++;
^
 ktermios *if (irt->co&niterm_
	uico
		if (it) > !cw		}
				Ychange_iN(1);
		re);
			}
			uart_change_speed(t, termios, oldedulpor abots++;
entses based;ios, orommon rt. we alNNIl idndiif (upu/ & rt->cons->cflag = tty->termios.dulHinter>0 and 0->1ion B0	-	get lettings.i ktermios *if (trt->ios.c_cfl TIO!(trt->ios.c_cflF_HARD_FLOW)
		uart_cet_termtrl(uport, TIOCM_RTS | TIdulHinter>0 and 0->1ia {

entseB0	-	get letti= 230400;!i ktermios *if (trt->ios.c_cfl TIO(trt->ios.c_cflFhange_port) {
, unsiuint3IOCM_RTS ->uts on!(trt->ios.c_cflagchan!gs);
tty)
		sTHROTTLNCB_SIO_ERROR, &)curr
	uin|t3trl(uport;rt) == 1)
				uart_set_mc
	uic frt->lock);
	Itra opens oist sIOCMh/w*	@tiB0 isf * Ae *srol,singnoo loaber ofWges t tryemake	 */
	
			uport-> por	/* Sht thatethe settings.
	 */
	if (uport->fmask & UPF_HAUNKNOWN)
	dulHinter>0NOWion (ffs.c_cflalettings.i ktermios *if (trt->ios.c_cflagcTIO!(trt->ios.c_cflag);
	for (;;) {
		spin_lock_irq(&uportu	 */
	
			uport->) {
			, flags);
	__uart_stl(uport);
		spin_unlock_irq(&uport->dulHinter>0NOWion (ns.c_cflaletti= 230400;!i ktermios *if (trt->ios.c_cflagcTIO(trt->ios.c_cflag);
	for (;;) {
		spin_lock_irq(&uportnfo->irq;
	if  !(uport->ops->get_mctrl(uport) & if (retval) {
			uport->hw_stopnable_ms)
		ucort->opquest_port(stl(uport);
		spin_unlock_irq(&uport-eturn retCmeatypo RD_FLOWoses)pt_shutdown are vi	/ng onty_dep: pn exceprommonslude/ste(No.c:ste(port->o() exceprommonslude/ste(No.c:c iste(us aga() exc;
}

/_po
entsea workemove cons * lsleepn baud _
 *_FL FiguP_SYS_Aking policy */
static Woses_info_user(struct tty_struct *t<lin *<liped long arg)
{
	struct uart_state *state = tty->driver_data;
	struct ttydriver_data;
	struct uart_;>uart_port;
	unsigned long fl
	 */
	_HAUNKNOWN)
	uart_port *uport = state->u tty_port *port = &st>uart_port;
	pr_d Wosesush_buffer(%d) uart_p?>line	    =  : -1port->mut!ine	  ruct uhanBUSY;
	ifelay  );
	__t_termiLO, <lipe
		if _HAUNKNOWN)
	duk);
	ptole useriuporlocaortax: maxnts exuntt seosing. Doorlok);
	rrupts  resorecee =_info	-	get lerial linehe settings.	 */
	if (uport->flags & ASYNC_INITIAver_data;
	unsigned lo;
	else {
		spin_locnlock_irq(&&port->lock,able_ms)
		ucort-rx->get_mctrl(uport);
		spin_unlock_inlock_irq(&&port->lock,IG_IRQ;
Befled bit 	 * TS ,emaphore to mted wipretend the _IRQ;
 porcoll dttRI drtated;ole us uses		/*
	ly_IRQ;
ismitta unito mt thapha * Free tht the! Die! Di;

		c int uart_wait_ty_str */
		irt_updatert->l = -EIO;

	mutex_lock(&portn.
		 */
		uart_shutdown(ttyBUSY;
	ifser(se__t_termty, state
	else {
		spin_lock_irqsave(&port->loctings.	 */
	bave(ed_ any);
	for (;;t);
		spin_unlock_irqrestore(&port->lockings.	 */
	y != port->ccurr
sleep(mutex_lock_inttruct elay	    = jiffies_to_msecs(port->c) lo;
	else {
		spin_locrqrestore(&port->lock->flags);
	) &&
			if (uart_con);
	for (;;t);
		spin_unlock_irqrestore(&port->locki);
			}
			uart_change_pm(state, UARTFF) lo;
	else {
		spin_locrqrestore(&port->lock-)
	duk);
	Waphore sny seetryactuall * pometers for this pown.
	 */
	clear_NORMAL_ACTIVEINITIALIZED, &portown.
	 */
	clear_?
			ASINITIALIZED, &portr (;;t);
		spin_unlock_irqrestore(&port->lockport-up(mutex_lock_int&ble_ms)
en_msr_waitport-up(mutex_lock_int&ble_mssecs(pmsr_wait,lock);
	}
	mutex_unlock(&port-nty_ddiscdebug(_uart_ste *st_wait  >) {
	IOCM_RTS);
}

staticc int uart_wait__break_ctl(struct tty_structirt_upday, char ch)
{
	struct uart_state *state = tty->driver_data;
	struct uart_port *port = state->uart_port;
	uns(uaruart_, expireoctings.	 */
	if (uport->type == PuhanTIALIZE
		uport-	if _HAUNKNOWN)
	duk);
	port
 *
? 1 :lerialutexally g1/5ait for  porm tty FiguPsn't emis usedit  lety XON/XOF*
 *	flakee whakert->t 1meout v? 1 :he transmiutex
 * Calnt. sy glAlwaeman
	 * Figure r thihe tralue
 	/*
	 * po Rs gk;
ntyht just thfy t  *  ion iRTSsfy not exi NIST-Pt) ort changesruart_	_ba */
		irt_upd -s) / blosiTIALIZE
		upor;angesruart_	_bgesruart_	/ 5;it;
		}
	ruart_	_	if _HAgesruart_	_b1et = -EBrt_upd ole(rt_upd <bgesruart_ _HAgesruart_	_b(rt_upd;->lock);
	Itra opretend the t poemovawn.
e transwi signa wpe txrm ttd if wm	taking aiguPsn timeout taitireht th,e whe tbptsy woemod if /* Shawn.
t semaphs t trso mted wipransmi sIOCMf *  count *srol,sUNT to onlnow;
	lydo this ft sHging,rt prop/* S not eakes;
	un levman
 */
		irt_upd,ole us use tbptsy duuPsn  sure  *	@stug.02 soguPkins
	 Soorlocclamp
	 * Figure t't set thhaksure 2* */
		irt_updhe settings. */
	port		ifhanB*/
	por> 2bits = biirt_upday		 */
	port-2bits = biirt_upd;->lexpire _delay	   +b(rt_upd;->lart_port;
	pr_dc int uart_wait_ush,delay	  =%lu, expire=%lu.. deprecaine	    = ,delay	  , expireempty(uport)C 1 :lwhe mt 'll pretend the traitter ies als'gesruart_'ber of'(rt_upd'	/ 'expire' ge =_us accoun *	@maxm	taking aigunded bitc inhe setti, flags!ble_ms)
		uport->opst_con);
	fo
sleep(mutex_lock_intelay	    = jiffiegesruart_ lockings.t */
		if (signal_pendinRESTARTSYS;
	ngs. */
s func(elay	  , expireenRESTARTSYS;
t-eturn retCmeatypo RD_FLus aga()rt_shutdown are serialinty_dep: pn exceprommonslude/ste(No.c:c iste(us aga() exc;
}

/_po
entsea workemove cons * lsleepn baud _
 *_FL FiguP_SYS_Aking policy */
staticus aga(t_tiocmget(struct tty_struct *tty)
{
	struct uart_state *state = tty->driver_data;
	struct tty_port *port = &staart_port;
	unsigned lonart_port;
	pr_dus aga(ush(%d) rqrestore(&state->unfo, Tet = -EIO;

	mutex_lock(&port->mu	 */
	if (uport->flaNORMAL_ACTIVE(port) == 1port->ops->fluart_stln.
		 */
		uart_shutdown(tty;
	else {
		spin_lock_irqsave(&port->lock, flagsruct u) {
			own.
	 */
	clear_NORMAL_ACTIVEINITIALIZED, &portor (;;t);
		spin_unlock_irqrestore(&port->lockiBUSY;
	ifser(se__t_termty, startnfo-> &&
			if (uarqrestore(&statelag & HUPCL)}
			uart_change_pm(state, UARTFF) lo;port-up(mutex_lock_int&ble_ms)
en_msr_wait;port-up(mutex_lock_int&ble_mseue(&port->dely, state, 1);
	}
	mutex_unlock(&poreturn ret;
}

static
	if wanvant uart_write(struct ttystruct *tty, struct tty_struc->mutex);
	return 0; */
staticc
	if void uart_shutdown(struct ttyct *retinfo)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport = state->ua(uport) will eue(&port->del emove sn   */
smemert-ks
 	/*m{

enee not exi )
		 *  iso
		 * Oove m just	/* Sht  wok poup
	 * te not exake suwoemo imeore >delion (nseue(&port->del zero. s

	/d if w Waiutt *ition <lin  strucplocsx
 * Caly gkriupate,
ksure  */
		ifser(f)
	 arite setti,ort-up(mutex_lock_int&ble_mseue(&port->dely, t->flags);

	/*
	 IRQ consrrupts  resos for this pable_ms)
		ucvoid uar->tx_empty(uport)nfo)
{
	/erial pIRQ stom e transmi/_pWion (nsanoo e tCPUed the poynchronporspin_nge_irq)
	poreturn ret;
}

staticarrie =ra arert_shutdown(struct ttyct *retinfo)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport = state->ate->t = uporr (;;) {
		spin_lock_irq(&uporicount));
	uart_enable_mt = u 
		result |= uport->ops->get_mctr(uport);
		spin_unlock_irq(&uporing.
 = u trl(uportARort->mutex)1et ->mutex);
	return 0; */
staticdt =rts uart_write(struct ttyst		unsnoffct *retinfo)
{
	struct uart_state *state = container_of(port, struct uart_state, port);
	struct uart_port *uport = state->uapbasenoffctt) == 1)
				uart_set_mctrl(uport, TIOCM_DTR | TCD_ENABLED_FLOW)
		uart_cet_termtrl(uport, TIOCM_DTR | Teturn retCmeatypo RD_FL * pot_shutdown are serialinty_dep: pn exceprommonslude/ste(No.c:ste( * p() excN

	/*
aunito m	 * ailsclearinRD_FLOWoses)p_
		 _shutdown ty 9600  In art_, bitc n the tcrClaim a" * pion nonorewait t_tes"00  be
	 ioges
onsall drives
o)
		ere/irvitc yn bau    tdownharacn is/tus e addrewses/t_tesd
		 st semaph
		 *ae * Aus acharalsr_ri		 * a cerstataxm	taking  hencegs);inst hangups.
 */
stati * p(_info_user(struct tty_struct *t<lin *<liped long arg)
{
	srommon *dr		}
(ng arg)
{
	srommon *)e *state = state = tt *upstate->porvr(&ninfo	= called\n",;*retinfo)
{
	struct uart_statdr	}

	curr+   = upover_data;
	struct tty_port *port = &stonart_port;
	pr_d * p(ush_buffer(%d) unfo, Tetduk);
	Weens.  Alsnfiguration *  ion g	pr n ee exake suwoemo bsore-e linednded b flagsource
	ince ch
	currver_da)
{ty, tb flagwe we fail w WaIRQs not exakecter ommon rt. i Not semaphst. s porim ani siside-es->datah
ksure el ort->orim aa * Thing 	pr_dus aga,iso
,
	 *  g	pr n ee exak't emi *port = &.ntyh
		 *ae {
		e *statout to loadRTSso));
	configuring.
	 */
	if (mutex_lock_interruptible(&pochange_irq) {
					ret = -ERESUNKNOWnd;;
		returngsruct OPB)
		bi!t *uport = statefhanrqrestore(&state->if (uport->fDEADochange_irq) {
		NXnt rT_UNKNOex_t_pc_ c)
			c-)
	duk);
	Oing,ween we  *state = tty->n *  ,e sure tg	pr n eed exak't emRD_FLOWoses)p
		 *_pcrdrivescter ommon rhat a
Rs gects pk);
	p Wa ail)
{o
entse *     wardsx
 * Cal portouch/*
	 * notconfigur  *state = tty->nort *upupoverestore(&state->art_statt *upupoveresto = &.ze;
	port->lorT_arqrestore(&statecy = (uport->flags & UPF_LOW_LATENCYiBUSY;
	ifser(se__t_termuart_s
	duk);
	p;
	_ore o shutdown the configur
	} else
		retval = uart_startup(t0empty(uport);

	/		/* Aedetilldel  uartra opens oist * If * under us.
	
	}
	mutex_unlock(&port			 */
			t-	if _HAUNK else
BUSY;
	ifbave(_art_ * If_t_termiLO, <lipemptWnd, 1);
 exit:
	retuex_t_pc_ c)
	:returngsruct --;er us.
	
	}
	mutex_unlock(&portUNKNOWnd;;	return 0;*tty,
t unsistatic		 rt_put_char(struct uarted lo*tty,
t unsit_p d);
			ort->mu	 */
	)
		up!= u	forg  sing_wai)
		up!= >unthrot)
		bi!t ru	forg  si"unkarin"ait, &wait)rg ;;	re#ifdef gs |= UPROC_FSreturn 0; */
staticunfoart_get_lsr_iseq_<lin *mner_of(port, srommon *dr	st		unict *retinfo)
{
	struct uart_statdr	}

	curr+ idriver_data;
	struct tty_port *port = &staenumchar(stmstruct tmstructtate, port);
	struct uart_port *uport = state->at unst *u>ops[32]iouort *port, un-	get ->ate->tmioot)
		bi!uart_poHAUNKNOWN)
	tmio ->io_type != upo>S)
	IO_MEval seq_prte-f(mne"%d
 *
 *:%s %s%08llX )
	:%dprecatline	    = , static		 ruart_precattmio ? "tmio:0x"ATE"art_:"recattmio ? >mapbase = (un*)(unsigned long)upo
nt->comm: >mapbase = (un*)(unsigned ld long ecatline	  )
	portings.
	 */
	if (uport->type == PO;
	foreq_putc(mne'\n' set, clN(1);
		re;
		}r;

	if (!capable(CAP_SYS_A = -EIO;

	mutex_lock(&port	tmstruct ort *uportmstructtatt->mu	mstruct !=e_pm(state, UARTNag & HUPCL)}
			uart_change_pm(state, UARTN(tty;
	else {
		spin_lock_irq(&uport-	get l
		result |= uport->ops->get_mctrl(uport);
		spin_unlock_irq(&uport->mu	mstruct !=e_pm(state, UARTNag & HUPCL)}
			uart_change	mstructuport-ock);
	}
	mutex_unlock(&port- seq_prte-f(mne" tx:%d rx:%dprecattline	  )* notctxstr */
		)* notcrxuport->mur */
		)* notcf setag & seq_prte-f(mne" fe:%dprecattline	  )* notcf setaport->mur */
		)* notce sizeag & seq_prte-f(mne" pe:%dprecattline	  )* notce sizeaport->mur */
		)* notcbrkag & seq_prte-f(mne" brk:%dprecattline	  )* notcbrkaport->mur */
		)* notcNNIl lnag & seq_prte-f(mne" oe:%dprecattline	  )* notcNNIl lnaort#defnfo	INFOBIT(biiner_o) \tings.
	 */
	
 = u tr(bii)) \tite, nce
(t *u>ops,
(ng )ort->icount*u>ops) - \titte, l p(_i*u>ops) - 2)t#defnfo	e, UBIT(biiner_o) \tings.-	get ltr(bii)) \tite, nce
(t *u>ops,
(ng )ort->icount*u>ops) - \tit ize ane, l p(_i*u>ops) - 2)tort-	ge>ops[0[cir'\0';ort-	ge>ops[1[cir'\0';ortINFOBIT(IOCM_DTR ne"|TR "aporte, UBIT(l(uport) ne"|CR "aportINFOBIT(IOCM_DTS ,e"|TS "aporte, UBIT(l(uports (e"|TS "aporte, UBIT(l(uporCA (e"|CD"aporte, UBIT(l(uporRASIN"|TI"lockings.t	ge>ops[0[ag & s	ge>ops[0[cir' 'ort- seq_puts(m,st *u>opsy, statreq_putc(mne'\n' se#ythif	e, UBITe#ythif	INFOBITreturn ret;
}

staticroc_
 *wet_lsr_iseq_<lin *mnese    vct *retinfo)a;
	rommon *a;
dr		}
mortrnvant;long arg)
{
	srommon *dr		}
a;
dr	state = tt *upstate->itate
eq_prte-f(mne"utdot_g:1.0r ommon%s%st *vis Th:%sdeprecat"d) "d) "dwait, &waiu) {
 i <tdr	}
nr
 i++ctt) == 1unfoart_gemnedr	st	port->mutex);
	return 0;
}

staticroc_ * p(_info_uinodn *inodnstruct *t<lin *<liestruc->mutexit  le_ * p(<lie,
staticroc_
 *w, PDE_DATA(inodn));;	return 0;*tty,
ruct *t<lin_ * ort cos
staticroc_f)
	 =SYS_.riner		= THIS_MODULE,S_.r* p		= staticroc_ * p,S_. * I		= 
eq_ * I,S_.llseek		= 
eq_lseek,S_. *rt->o	= 
t  le_ *rt->o,
}se#f (sfre#if defnfod(gs |= USERIAL_COREags SOLEgchandefnfod(gs |= Ugs SOLE_POLL)urn re	 &&
			if (uic int - c int a 		if (u message sn  hutdown the ing qart_:ra opens osn c int accouessageth the:ureraWait y XON/XOFsth th c)
	: numb* Get y XON/XOFstranrucactuallc intth thc int:
/*
 * Thisn c int y XON/XOFF c the ing/
 */
static 	if (uic intrt_put_char(struct uart,;*tty,
t unsiold seort *port, uns &upod se */
s(*putcps->rt_put_char(struct st		u)structrt *port, unitate, &waiu) {
 i <t c)
		 i++str++cNITIALIZE*supor'\n' d seputcps-_t_term'\r' set,putcps-_t_term*>lock, flEXt->tySYMBOL_GPL(tatic 	if (uic intaortrn re	C 1 :lwhe mt '* l n	re/
stati numb* G port that		/*fietilaneing i2 somctrarch/ resourt<lril wvailpts  ens osxakeon/r*
	 *ing 		if (u supthe cot hangput_char(struct  _arti/
statiport		if (uarqput_char(struct uartsst		unnrstruct *t		if (u *cser *n, unidxtate led\n",;*tings.idxt<	ifhanidxt_info>= nruarts[idx].
			upor		if (itnt->commuarts[idx].mem		upor		ty, s d s, &waidxtat{
 idxt<	nr
 idx++ctt)t->mu	 */s[idx].
			upo!		ifha
nt->comuarts[idx].mem		upo!		ty, sitnt-ZRTSYS; &  led\n",tatidxIOCSER_TEMT;artsr+ idxIOrn retval;
}

/e ssn_ *t cos
- P ssnhutdown the s new/e size/ num/f * Ae *srol.th th *t cos:gkriupOFF c  *t conrucactth th new:gkriupOFF c * l'iup'	v sipts   resourt new	 * n.th the size:gkriupOFF c * l'iup'	v sipts   resourte size.th th num:gkriupOFF c * l'iup'	v sipts   resourtnumb* Get ty->n num.th thf * :gkriupOFF c * l'iup'	v sipts   resourtf * Ae *srol y XON/XOFy 9600 ;
}

/e ssn_ *t cos
decodnpha rucactue *stat	ince ch
tdown 		if (u00 ; *t cosmeout v rematait for rucactuis < new><e size>< num><f * >,00 ;eg: 115200n8ring/
 */


}

/e ssn_ *t cos(t unsi *t cosst		un* newst		un*e sizest		un* itsst		un*f * ed lo* unsiotat *t cosIOCS* new ortall d(strtoul(srmty, ,ose_del, flagsiot_in'0' oleiot<in'9' d ssOPB)
		bi*s->en*e size or*sOPB)
		bi*s->en* numbor*sOP
- '0';or		bi*s->en*f * Aor*s; flEXt->tySYMBOL_GPL(tatice ssn_ *t cosaortruct *t new_ * ns ructrt *port, unr*upstaort *port, unsigne;
}seeturn 0;*tty,
ruct *t new_ * ns  new_ * ns[] =SYS_{ 921600, B921600 },S_{ 460800, B460800 },S_{ 230400, B230400 },S_{ 115200, B115200 },S_{  57600, B57600  },S_{  38400, B38400  },S_{  19200, B19200  },S_{  baud_, B9600   },S_{  b4800, B4800   },S_{  b2400, B2400   },S_{  b1200, B1200   },S_{  bbbb0, B38400  }
}seeretval;
}

/)
		 *t cos
- is/*
	e ch
tdown 		if (ut't set thsing qart_:rkriupOFF c o shutdown the schar(structver_da)
{th th c: 		if (ut'riupOFth th new:g new	 * nth the size:gk size y XON/XOFF- 'n' (none)or'o' (odsh,d'e' (e.  )th th num:gnumb* Get ty->n numth thf * :gf * Ae *srol y XON/XOFF- 'r' (e s)ing/
 */
stati)
		 *t cosrt_put_char(struct uart,;ruct *t		if (u *cs confruct newst		une sizest		un itsst		unf * ed lotate,
					strucs based;povere 0;tate,
					strucdummystate->itate(uport)nfo)
{
	/erial p
tdown 		if (utdep: pstraitown ared if /arlf * un	Itra ters fohapha 		if (uclearinal p
pindep: pst		 * Ifhe tranitown areconfiguring.!(tatic 	if (u_port_uTIO(turngsrunscy = (uporgs _ENABLED)g);
	for (;;) {
		nit_irqrestore( set,ore(depi)
		class_irqrestore(&pirqre;) {
	keyatert->l t_port&			struset(retinfo,tate,
					struof(*re
	if (tty->termi= CREAD | HUPCL | CLOCval = (uport)Ctty,te,
	aots++;
r rify configur, &waiu) {
  new_ * ns[i].r*ups i++ctt)ing. new_ * ns[i].r*upt<in newnRESTARTSYS;re
	if (tty->termi|=  new_ * ns[i].>termios.ing. numbo= 7ay		 	if (tty->termi|= CS7 TCD_ENABL 	if (tty->termi|= CS8tate
arity *e sizeaag & CSIZ'o':  CSIZ'O':ABL 	if (tty->termi|= PARODD;ck,IGfaer t
roughu/ & CSIZ'e':  CSIZ'E':ABL 	if (tty->termi|= PARENB;ck,ZRTSYS;
exit;
		f * Ao= 'r'ay		 	if (tty->termi|= Cc_cflal = (uport)soguPtatithe coo e tsidet so
	 supthe sistf * Ae *srol.t);
	po,ween we* TS tranhos_char(n semaphoo em* inters on e	 */
	
 = u |t3IOCM_RTS ->
& mask) {
		port->ops->suart,;&			struse&dummy| TIduk);
	per* A to verify ait for  *	@s't set ths assocamty,  		if (u0not eoo:onfiguring.cser	&  le rt->consag = tty->termios.->mutex);
	rEXt->tySYMBOL_GPL(tatic)
		 *t cos)se#f (sf0;
	gs |= USERIAL_COREags SOLEfigueretval HUPCL)}
			uar
- is/rs werh
	currxtra opens  9600  he UART
the s strucploc00  h	mstruct:or thtruct 9600  L {
fy : ex_unlock(&s pori sy gheldAking policy */
static }
			uart_cct *tty, struct uart_state *  enumchar(stmstruct tmstructed long arg)
{
	struct art_port *uport = state->uapbast *uportmstructlosinmstructeNITIALIZE mask) {
		pm d sepmask) {
		pmsuart,;nmstruct,rt *uportmstructuport-	geportmstructl= tmstructtat};	retu arg)
{
	smatty  long arg)
{
	struct art_;long arg)
{
	srommon *drmmon;
}seeturn 0;, unsi  strmattyops->cng arg)devi si*ice(ese    ty->ed long arg)
{
	smatty *matty = ty->driver_data;
	rommon *a;
_dr		}
mattystate = sta;
	rommon;
f[64]g)dev_porMKDEV(a;
_dr	 lonjustoa;
_dr	 loinortval =) +ort-attystine	    = ios.->mutexdevoticeort		iceo;0;
	Ada)
	ly,l  RI  seetze ps toruct /;	re
}

staticusif (ops->cng arg)rt, srommon *dr	st_enable_ms(struct uart_port etinfo)
{
	struct uart_statdr	}

	curr+ uine	    = ioiver_data;
	struct tty_port *port = &state->pordevi si*a;
_dev;long arg)
{
	smatty matty = {et_termdr	} flart_port;

	mutex_lock(&port-a;
_dev =rdevi s_fd\nc }ild	dev_notice( &matty,nsi  strmattyops->port			 devi s_may_,ort uart__dev)eNITIALIZE!t));
	unfo-,ort_nge_irq)
	pORT_UNKNOWN)nfo-,ort>hw_stop;

	devi sart__dev)port-ock);
	}
	mutex_unlock(&por	.->mutex);
stat;

	devi sart__dev)pouring.csif (uicusif (ot));
	dchan! &&
			if (uart_con)
_UNKNOWN)cusif (t->hw_sttings.	 */
	if (uport->flags & ASYNC_INITIA*tty,
ruct *t	pr_d *uct p l
		result |=ockinvescriesERRUPTIBLcsif (uicusif (ot));
	dchan! &&
			if (uart_con)if (re)
		 */
	clear_SUSPENDNCB_S	 */
	if (upor	.	own.
	 */
	clear_gs & ASYNC_B_S	 */
	if (upor (re)	else {
		spin_lock_irq(&uport	)
		ucort->opquest_port	 {
		portuart_cet_term0uport	)
		ucort-rx->get_mctrll(uport);
		spin_unlock_irq(&uportval = 0;
		}
;
}

/*
 ll pretend the tKNOW->oprt is foun, &wacriesl
	3; !t = uport->ops->tx_e ole(riesEe(ries--ccurr
sleep(se_delALIZE!(riesccurr[64]err	dev_notice( "%s%d
 Unpts  ro drtatpretend the deprecatedr	sta64];setrecatedr	sta;
	rommon->;set_info-+ uine	    = )ERRUPTIBLcsif (uicusif (ot));
	dchan! &&
			if (uart_con)ort	)
		ucvoid uar->tx_empc-)
	duk);
	Drupts  reso		if (utdevi sibefled cusif (to-configuring.csif (uicusif (ot));
	dcole &&
			if (uart_con)ortcsif (uicort	dev_notccosaortring.csif (uicusif (ot));
	dchan! &&
			if (uart_con)
_UNUPCL)}
			uart_change_pm(state, UARTFF) l->lock);
	}
	mutex_unlock(&port->mutex));
	re
}

stati;

	meops->cng arg)rt, srommon *dr	st_enable_ms(struct uart_port etinfo)
{
	struct uart_statdr	}

	curr+ uine	    = ioiver_data;
	struct tty_port *port = &state->pordevi si*a;
_dev;long arg)
{
	smatty matty = {et_termdr	} fotate,
					strucs based;plart_port;

	mutex_lock(&port-a;
_dev =rdevi s_fd\nc }ild	dev_notice( &matty,nsi  strmattyops->port			 !NKNOWN)cusif (t->oledevi s_may_,ort uart__dev)eNITIALIZENKNOWN)nfo-,ort)if (rerrupts unfo-,ort_nge_irq)
	p;RT_UNKNOWN)nfo-,ort>hw		retvaop;

	devi sart__dev)port-ock);
	}
	mutex_unlock(&por	.->mutex);
stat;

	devi sart__dev)poUNKNOWN)cusif (t->hw0l = (uport)Re-e pts  reso		if (utdevi si func cusif (to-configuring. &&
			if (uart_con)if (rgshift;

lril  new poRs greso		if (utts++;
r rify conis foun t_port&			struset(retinfo,tate,
					struof(*		 	if (tty->termim_divisor unscy>termios. state);

			/er'sate igneds gresontyht		strucr rify conis founngs.	 */
	ityhole(	if (tty->termim	if _HA	 	if (t sing_wais->cflag = tERRUPTIBLcsif (uicusif (ot));
	dag & HUPCL)}
			uart_change_pm(state, UARTN(tty;able_ms)
		uc in->ops->suuart,;&			strusety, startnfo-csif (uicusif (ot));
	dag & csif (uicoa->configotccosaor
exit;
			 */
	if (uport->flaSUSPENDNCINITIA*tty,
ruct *t	pr_d *uct p l
		result |=ockinvescnow;
	 HUPCL)}
			uart_change_pm(state, UARTN(tty;
	else {
		spin_lock_irq(&uport {
		portuart_cet_term0uport(uport);
		spin_unlock_irq(&uport->mucsif (uicusif (ot));
	dchan! &&
			if (uart_con)if (re;
	P* This isbytoructock(&s, &wnowN only._info_user(struct tty_ sing_wais->;RT_Uent)) )
		ucol = ua>get_mctrll;
		ts))m	if F_SPD_M>mutx);	if (retUPCL)}
			uart_change_speed(tty, statere)	else {
		spin_lock_irq(&uport	t {
		portuart_cet_term
	 */
	
 = uuport	t {
		pttaticx->get_mctrllt(uport);
		spin_unlock_irq(&uportre)
		 */
	clear_gs & ASYNC_B_S	 */
	if (upor	rt(uport);
		}KNOWN;

			F	 * If we fa try chaybitialise thwives
way?WN;

			Cwill im a"anitown zed" s++;
roe suwoemo nters;

			I,DSaer t
it * Ask the low lsx
 oid ua  to odiled anyway.
	n.
		 */
		uart_shutdown(tty;tvaop		breawn.
	 */
	clear_SUSPENDNCB_S	 */
	if (upor	}l->lock);
	}
	mutex_unlock(&port->mutex));
	return 0;, info	 */


}

/rerqre;ps->cng arg)rt, srommon *dr	st_enable_ms(struct art_port * unsaddrews			char 
arity *eotype != upaag & CSIZ
	IO_t->t:ABLsnprte-f(addrews(retinfo,addrews)( "I/O 0x%lx", eotype !info);ck,ZRTSYS;
 CSIZ
	IO_HUB6:ABLsnprte-f(addrews(retinfo,addrews)(ate *"I/O 0x%lx (ffis/r0x%x", eotype !info, eotypehub6);ck,ZRTSYS;
 CSIZ
	IO_MEM:;
 CSIZ
	IO_MEM32:;
 CSIZ
	IO_AU:;
 CSIZ
	IO_TSI:ABLsnprte-f(addrews(retinfo,addrews)(ate *"MMIO 0x%llx", >mapbase = (un*)(unsex_unlong)upo);ck,ZRTSYS;

		break
tte, llockaddrews(r"*unkarin*"(retinfo,addrews));ck,ZRTSYS;
		retrte-k(KERN_INFO "%s%s%s%dhake%s ()
		= %d,tus e_ new orush_apha %sdeprec ize anev_notice ? a64];set(ev_notice)ATE"prec ize anev_notice ? "TE"ATE"prec ize andr	sta64];setrec ize andr	sta;
	rommon->;set_info-+ ine	    = rec ize anaddrews(rKNOWN)nfo(rKNOWN)_ms(upor/ 16, static		 rt_con);
	return 0; */

 &&
			irt typ;ps->cng arg)rt, srommon *dr	st_enable_ms(struct uart_statee ane, able_ms(struct art_port ort *port, unigned lonock);
	Itra o thapemo es if
	 *  ,e so
	 dosn't chan fura o configuring.!eotype !infocTIO!ex_unlong)upocTIO!ex_unloem		up _HAUNKNOWN)
	duk);
	N* Adc o shnt uhe
	 * port conftuff
	 * te
	/eriport->ops->he tra: Thphis is uheThis will the ports *	flapra opens o, &w
	 * underuport);

	retval	 */
	if (uport->flags & UPF_ATO_IRQ)
			flags |= UART_Cetval	 */
	if (uport->fBOOTflagsgs |eNITIALIZE!l	 */
	if (uport->flags &ART_n)if (reng_waisf (uprt->type == Ptty;tTO_IRQ)
			flags |= UART_CONFvaop;le_ms)
		uport->ops->ct_termif (upor	}l->ngs.	 */
	if (uport->type != PONITIAver_data;
	unsigned l
	 HUPCLrerqre;ps->cdr	stget_mct
re;
	P werhuppens o, &wportuart_c)ie! Di;

		)}
			uart_change_pm(state, UARTN(tts. state);
nfo)
{
	/erial py of te *srol in arge thde- wanvantort is  keepnal pTS tverify a*
auniucr rtranstati)
		 *t cosr);
		}
;ese tbptsy dso
	 i No
ap
pindep: ara pog. Doorbutonis foun
	else {
		spin_lock_irqsave(&port->lock, flags {
		portuart_cuart,;n */
	
 = u		    ((arS | TIor (;;t);
		spin_unlock_irqrestore(&port->locs. state);

			/ist ommon supthe s 		if (ucl
onsatt poemovt thate);
	/* Assfulsy ine statNo
yigne new pore-ine statustrt is  It rt. b{
	/erial pens owpor	ol wvailpts conis founngs.	 */
			ifcTIO!(ivisor unscy = (uporgs _ENABLED)gRT_Uene statc 	if (u_portotccosaortr state);
P werhd ua aer the s byt
		brea,are counAlso,count *f (utif 	/*
	 * on conis founngs.! &&
			if (uat_con)ort	HUPCL)}
			uart_change_pm(state, UARTFF) lo};	re#ifdef gs |= Ugs SOLE_POLLturn ret;
}

static
ll		nit_ver_data;
	rommon *rommonst		un  = , t unsi *t cosed long arg)
{
	srommon *dr		}
ate = state = tt *upstaetinfo)
{
	struct uart_statdr	}

	curr+   = upover_dat
{
	struct art_;loruct newtat9600;loruct numbor8;loructe size or'n';loructf * Aor'n';loructint flags, r
	currhan!rqrestore(&statelr	.->mutex-1->
& masport *uport = state->atIZE!l	 */
	 {
		p
ll	port	 unsTIO	 */
	 {
		p
ll	;

	cps->lr	.->mutex-1->
&LIZE mask) {
		p
ll		nit);
	forg arg)a;
	structt tty_port *port = &st
&cts))) {
			rt_port;

	mtex_unlock(&por	. 0;
		}
;et so
	 ss))	clear_gs & ASYNC_ porw    RI anitown zednAlso,counhw,ar.g.rt *uporng(&niucrt		 *unanitown zedconis founngs.!gs);
tty)	clear_gs & ASYNC_B_St	 */
	if (up)current))  mask) {
		p
ll		nit>unthrotrt-ock);
	}
	muttex_unlock(&por	.;
		ts))currentutex);
	r	}l->ngs. *t coseport) == 1e ssn_ *t cos( *t cosst& newst&e sizest& itsst&f * eor	.->mutexstati)
		 *t cosruart,;ty, ,o newste sizest itsstf * eor	}rt->mutex));
	return 0;, 

static
ll	port	 un_ver_data;
	rommon *rommonst		un  = ed long arg)
{
	srommon *dr		}
ate = state = tt *upstaetinfo)
{
	struct uart_statdr	}

	curr+   = upover_dat
{
	struct art_;llags, r
	currhan!rqrestore(&statelr	.->mutex-1->
& masport *uport = state->aER_TEMT;art
	 {
		p
ll	port	 un>unthrot	return 0; */
staticc
ll	;

	cps-_ver_data;
	rommon *rommonst		un  = , t unsched long arg)
{
	srommon *dr		}
ate = state = tt *upstaetinfo)
{
	struct uart_statdr	}

	curr+   = upover_dat
{
	struct art_;llags, r
	currhan!rqrestore(&statelr	.->mute->
& masport *uport = state->tring.chupor'\n' d s	 */
	 {
		p
ll	;

	cps-_t_term'\r' set	 */
	 {
		p
ll	;

	cps-_t_termcheot	r#f (sfreturn 0;*tty,
ruct *tste( * ort cos
stati)
	 =SYS_.r* p		= stati * p,S_.OWose		= statiOWose,S_.c int		= static int,S_.;

	cps-	= static

	cps-,S_.port->cps-s	= statiport->cps-s,S_.c int_room	= static int_room,S_.Ops-s		n>ops->f= statiOps-s		n>ops->f,S_.port->ops->f	= statiport->ops->f,S_.Note:		= statiNote:,S_.t
rottle	= statit
rottle,S_.unt
rottle	= statiunt
rottle,S_.time_xcps-	= statitime_xcps-,S_.tiin->ops->	s);

		c in			strusS_.tiinddisc	s);

		c inddiscsS_.ttop		= statittopsS_.tttat		= statitttatsS_.us aga		= statius aga,S_.port->ops	= statiport->ops,S_.c int uart_waits);

		c int uart_wait,e#ifdef gs |= UPROC_FSr_.;roc_f)
		= &staticroc_f)
	,r#f (sfr_.tNotmpor	= statitNotmpor,r_.tNotmsor	= statitNotmsor,r_.c int uart	) static inw, &upoe#ifdef gs |= Ugs SOLE_POLLt_.;
ll		nit	= static
ll		nit,t_.;
ll	port	 un	= static
ll	port	 un,t_.;
ll	;

	cps-	= static
ll	;

	cps-,r#f (sfr}seeturn 0;*tty,
ruct *tBUSY;
	if * ort cos
staticoati)
	 =SYS_. wanvant	= static
rif wanvantsS_.t*/
		ua	= static
rift*/
		ua,S_.Oarrie =ra arese
		retOarrie =ra are,S_.dt =rts	= statidt =rts,
}seeretval;
}

/ene statcrommon -line statuat ommon assocountear(ncled layOFth thdr	:t * Ask the low ltver_da)
{th th tRne statuatear(n ommon assocountcled  low l.s
;etatprutex);e statth tassocountty_ layOFcl
onsanitown arcountcled  low l * Take thet* n.th th tWe we alrearoct<lin atp/aroclude/ ommon aNT to on;setdi func Alsoh tnremahe low l.th th tdr	}
ke the * Caly gty, ,oameout t* Take thetr_da)
{sx
 * Caly th tine statNo
 TIOCM
}

/->loon struct func Al onlaer  por	/* Aedet.ing/
 */ 
}

/ene statcrommoncng arg)rt, srommon *dr	ct *retinfo)a;
	rommon *nremah;loructi,it:
	retu
	BUG_ON(dr	}

	curempty(uport)Maybitween * Caly g TIOCMap
lptnlach   resouDoores		/*
	lys resed bitwe alrelargrtnumb* Get the s to stom e * underdr	}

	curr= kzsourc(t->icount, sizeof(structeN*tdr	}
nr, GFP_KERNELport			 !dr	}

	cure
T_UNKNOout flanremahe=gsourc_a;
	rommon(dr	}
nrport			 !nremahe
T_UNKNOout_keneeS; &
r	sta;
	rommone=gnremah;llanremahstate = t;set	atdr	}
ate = t;set;lanremahst;set		atdr	}
a64];set;lanremahstonjus		atdr	}
onjus;lanremahstoinortval =	atdr	}
oinor;lanremahstif (		= TTY_DRIVERUART_USERIAL;lanremahstsubif (		= SERIAL_ART_UNORMAL;lanremahstanitn->ops->	s)ser(sttermios *;lanremahstanitn->ops->ty->termim_B9600 | CS8 | CREAD | HUPCL | CLOCval anremahstanitn->ops->ty-iets cu= nremahstanitn->ops->ty-oets cu=t9600;lonremahstif (u		= TTY_DRIVERUREAL_RAW, TITY_DRIVERUDYNAMIC_DEV;lanremahstate = t
	curr and dr	;t-a;
_)
		 * ort cos(nremah, &stati)
	empty(uport);nitown arcount *	@s
	cur(s)configur, &waiu) {
 i <tdr	}
nr
 i++c;
	forg arg)
{
	struct uart_statdr	}

	curr+ idriover_data;
	struct tty_port *port = &stoniBUSY;
	if	nit>unthrotrt	 */
	 {
 = &staticoati)
	otrt	 */
	secs(port->ze and HZ / 2;e;
	.5 ss*ttd lettit	 */
	secsIOC->del  and 30rt)HZ;;
	30rss*ttd letti}rt->mu else
BUSYene statcrommoncnremaheort			 >mu els>	if _HAUNKNOWit:
	retu
	, &waiu) {
 i <tdr	}
nr
 i++ctt)BUSY;
	ifds);roy(&dr	}

	cur[i].unthrotr;

	a;
	rommon(nremaheorout_kenee:
	kenee(dr	}

	curempout:rt->mutex	iNOMEvalrn retval;
}

/unene statcrommon -linUNNIuat ommon entsesuntear(ncled layOFth thdr	:t * Ask the low ltver_da)
{th th tRnUNNIuaer re->fgings sn  h ommon entsesuntcled  low l.s
T
it * th tsk the low ltmusttwe alinUNNIduaer numbthe s vi	/ng val;
}

/enUNNINon strucs)pr propine statNo
o em*assoc
}

/->loon struc().th t(ie, dr	}
ke thr		ty, sing/
 */
staticunene statcrommoncng arg)rt, srommon *dr	ct *retinfo)a;
	rommon *ptatdr	}
a;
	rommon;
ftrt *port, unitatea;
	unene statcrommoncprotr;

	a;
	rommon(pwait, &waiu) {
 i <tdr	}
nr
 i++ctt)BUSY;
	ifds);roy(&dr	}

	cur[i].unthrotrkenee(dr	}

	curemprdr	}

	curr= ;
			o&
r	sta;
	rommone=g;
			o	retu arg)a;
	rommon * &&
			if (u	devi saruct *t		if (u *cs t		un*d\n",ed long arg)
{
	srommon *ptate lety->dri*d\n",tate led\n",;*aER_TEMT;}
a;
	rommon;
	return 0;st->i_/
static inatt =_ms(upocng arg)devi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp. new_)upoc* 16);
	return 0;st->i_/
static inatt =c		 rt_put_cdevi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp.= upa;
	rturn 0;st->i_/
static inatt =  = rt_put_cdevi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp.info, TIOCM_RTS);st->i_/
static inatt =ps->cng arg)devi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)poAver_data;
	unsioaddrpourstatic inwt_get_term&ampportioaddrse
Bmp.tate->atIZEHIGH_BITSRTFFSETctt)ioaddrs|= >mapbase = (un)Bmp.tate_high>flaHIGH_BITSRTFFSETort->mutexsnprte-f(ops,
PAG_USIZEne"0x%lX(%d) ioaddr, TIOCM_RTS);st->i_/
static inatt =pin_ng arg)devi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp.)
	poreturn ret;st->i_/
static inatt =if (u_ng arg)devi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"0x%X(%d) amp.ort->loceturn ret;st->i_/
static inatt =ng(&_E
		_t->i_ng arg)devi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp.ng(&_E
		_t->ilocetuurn ret;st->i_/
static inatt =secs(port->_ng arg)devi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp.secs(port->cocetuurn ret;st->i_/
static inatt =secsIOC->del_ng arg)devi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp.secsIOC->delloceturn ret;st->i_/
static inatt =custom_divisor_ng arg)devi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp.sustom_divisor, TIOCM_RTS);st->i_/
static inatt =po=c		 rt_put_cdevi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp.po=c		 , TIOCM_RTS);st->i_/
static inatt =pomem_)uport_put_cdevi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"0x%lX(%d) >mapbase = (un)Bmp.pomem_)upo, TIOCM_RTS);st->i_/
static inatt =pomem_ene_shifl_ng arg)devi si*ice(tate->pordevi snatt ibuct uatt , t unsiopsyd long arg)si  strtu arg)ampioiver_data;
	struct tty_poa64]c in
r	ty->(dev)pourstatic inwt_get_term&ampport->mutexsnprte-f(ops,
PAG_USIZEne"%d(%d) amp.pomem_ene_shifl, TIOCM_RTS);DEVICE_ATTR(c		 , S_IRUSt, TS_IRGRP,
static inatt =c		 (tty, staM_RTS);DEVICE_ATTR(  = , S_IRUSt, TS_IRGRP,
static inatt =  = , ty, staM_RTS);DEVICE_ATTR(t_termS_IRUSt, TS_IRGRP,
static inatt =uart,;ty, staM_RTS);DEVICE_ATTR(nfo(rS_IRUSt, TS_IRGRP,
static inatt =nfo(rty, staM_RTS);DEVICE_ATTR(ort->(rS_IRUSt, TS_IRGRP,
static inatt =ort->(rty, staM_RTS);DEVICE_ATTR(ng(&_E
		_t->i(rS_IRUSt, TS_IRGRP,
static inatt =ng(&_E
		_t->i(rty, staM_RTS);DEVICE_ATTR(_ms(upo(rS_IRUSt, TS_IRGRP,
static inatt =_ms(upo(rty, staM_RTS);DEVICE_ATTR(secs(port->(rS_IRUSt, TS_IRGRP,
static inatt =secs(port->(rty, staM_RTS);DEVICE_ATTR(secsIOC->del(rS_IRUSt, TS_IRGRP,
static inatt =secsIOC->del(rty, staM_RTS);DEVICE_ATTR(sustom_divisor(rS_IRUSt, TS_IRGRP,
static inatt =sustom_divisor(rty, staM_RTS);DEVICE_ATTR(no_c		 , S_IRUSt, TS_IRGRP,
static inatt =no_c		 , ty, staM_RTS);DEVICE_ATTR(nomem_)upo, S_IRUSt, TS_IRGRP,
static inatt =nomem_)upo, ty, staM_RTS);DEVICE_ATTR(nomem_ene_shifl, S_IRUSt, TS_IRGRP,
static inatt =nomem_ene_shifl, ty, statvere 0;tate,
	att ibuct urt__devnatt s[] =SYS_&devnatt _c		 .att ,S_&devnatt _  = .att ,S_&devnatt _uart.att ,S_&devnatt _nfo.att ,S_&devnatt _ort->.att ,S_&devnatt _ng(&_E
		_t->i.att 