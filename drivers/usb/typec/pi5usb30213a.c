// SPDX-License-Identifier: GPL-2.0+
/*
 * TI PI5USB30213A Type-C DRP Port Controller Driver
 *
 * Copyright (C) 2019 Renesas Electronics Corp.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/usb/role.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/usb/typec.h>
#include <linux/delay.h>

/* Register PI5USB30213A_REG_GEN_CTRL*/
#define PI5USB30213A_REG_CONTROL_PORT_SETTINGS_MASK		(BIT(6) | BIT(2) | BIT(1))
#define PI5USB30213A_REG_CONTROL_PORT_SETTINGS_DEVICE		0x00
#define PI5USB30213A_REG_CONTROL_PORT_SETTINGS_HOST		BIT(1)
#define PI5USB30213A_REG_CONTROL_PORT_SETTINGS_DRP		BIT(2)
#define PI5USB30213A_REG_CONTROL_PORT_SETTINGS_DRP_TRY_SNK	(BIT(6) | BIT(2) | BIT(1))
#define PI5USB30213A_REG_CONTROL_PORT_SETTINGS_DRP_TRY_SRC	(BIT(2) | BIT(1))

#define PI5USB30213A_REG_CONTROL_PWR_SAVING			BIT(7)
#define PI5USB30213A_REG_CONTROL_ACC_DETECTION			BIT(5)

#define PI5USB30213A_REG_CONTROL_CHARGING_CURRENT_MASK		(BIT(4) | BIT(3))
#define PI5USB30213A_REG_CONTROL_CHARGING_CURRENT_DEFAULT	0x00
#define PI5USB30213A_REG_CONTROL_CHARGING_CURRENT_MEDIUM	BIT(3)
#define PI5USB30213A_REG_CONTROL_CHARGING_CURRENT_HIGH		BIT(4)

#define PI5USB30213A_REG_CONTROL_INTERRUPTS			BIT(0)


/* Register PI5USB30213A_REG_INTERRUPT */
#define PI5USB30213A_REG_INTERRUPT_FAULT_EVENT_REC		BIT(7)
#define PI5USB30213A_REG_INTERRUPT_OCP				BIT(6)
#define PI5USB30213A_REG_INTERRUPT_OVP				BIT(5)
#define PI5USB30213A_REG_INTERRUPT_OTP				BIT(3)
#define PI5USB30213A_REG_INTERRUPT_FAULT			BIT(2)
#define PI5USB30213A_REG_INTERRUPT_DETACHED			BIT(1)
#define PI5USB30213A_REG_INTERRUPT_ATTACHED			BIT(0)

/* Register PI5USB30213A_REG_CC_STATUS */
#define PI5USB30213A_REG_CC_STATUS_VBUS				BIT(7)
#define PI5USB30213A_REG_CC_STATUS_CHARGING_CURRENT_MASK	(BIT(6) | BIT(5))
#define PI5USB30213A_REG_CC_STATUS_CHARGING_CURRENT_STANDBY	0x00
#define PI5USB30213A_REG_CC_STATUS_CHARGING_CURRENT_DEFAULT	BIT(5)
#define PI5USB30213A_REG_CC_STATUS_CHARGING_CURRENT_MEDIUM	BIT(6)
#define PI5USB30213A_REG_CC_STATUS_CHARGING_CURRENT_HIGH	(BIT(6) | BIT(5))

#define PI5USB30213A_REG_CC_STATUS_ATTACHED_STATUS_MASK		(BIT(4) | BIT(3) | BIT(2))
#define PI5USB30213A_REG_CC_STATUS_ATTACHED_STATUS_STANDBY	0x00
#define PI5USB30213A_REG_CC_STATUS_ATTACHED_STATUS_DEVICE	BIT(2)
#define PI5USB30213A_REG_CC_STATUS_ATTACHED_STATUS_HOST		BIT(3)
#define PI5USB30213A_REG_CC_STATUS_ATTACHED_STATUS_AUDIO	(BIT(3) | BIT(2))
#define PI5USB30213A_REG_CC_STATUS_ATTACHED_STATUS_DEBUG	BIT(4)

#define PI5USB30213A_REG_CC_STATUS_PLUG_POLARITY_MASK		(BIT(1) | BIT(0))
#define PI5USB30213A_REG_CC_STATUS_PLUG_POLARITY_STANDBY	0x00
#define PI5USB30213A_REG_CC_STATUS_PLUG_POLARITY_CC1		BIT(0)
#define PI5USB30213A_REG_CC_STATUS_PLUG_POLARITY_CC2		BIT(1)
#define PI5USB30213A_REG_CC_STATUS_PLUG_POLARITY_UNDETERMINED	(BIT(1) | BIT(0))


struct pi5usb30213a {
	struct i2c_client* client;
	struct device *dev;
	struct usb_role_switch	*role_sw;
	struct typec_port *port;

	struct {
		u8 id;
		u8 control;
		u8 interrupt;
		u8 status;
	} regs;
};

/*
	PI5USB30213A does not support i2c data byte addressing for read/write operation
	Every time all 4 registers need to be read/written
*/
static int pi5usb30213a_i2c_write_regs(struct pi5usb30213a *pi5usb30213a)
{
	int ret = 0;

	u8 data[4];
	data[0] = pi5usb30213a->regs.id;
	data[1] = pi5usb30213a->regs.control;
	data[2] = pi5usb30213a->regs.interrupt;
	data[3] = pi5usb30213a->regs.status;

	ret = i2c_master_send(pi5usb30213a->client, (char*)&data, sizeof(data));

	if (ret < 0)
		dev_err(pi5usb30213a->dev, "cannot write register\n");

	return ret;
}

static int pi5usb30213a_i2c_read_regs(struct pi5usb30213a *pi5usb30213a)
{
	int ret = 0;
	u8 data[4] = {0};

	ret = i2c_master_recv(pi5usb30213a->client, (char*)&data, sizeof(data));

	dev_dbg(pi5usb30213a->dev, "regs=%02X %02X %02X %02X\n", data[0], data[1], data[2], data[3]);

	if (ret < 0) {
		dev_err(pi5usb30213a->dev, "cannot read registers\n");
		goto done;
	}
	if (ret != sizeof(data)) {
		dev_err(pi5usb30213a->dev, "only read %d/%ld bytes\n", ret, sizeof(data));
		ret = -EIO;
	}

	pi5usb30213a->regs.id = data[0];
	pi5usb30213a->regs.control = data[1];
	pi5usb30213a->regs.interrupt = data[2];
	pi5usb30213a->regs.status = data[3];

done:
	return ret;
}


static int pi5usb30213a_set_port_mode(struct pi5usb30213a *pi5usb30213a, int src_pref)
{
	pi5usb30213a->regs.control &= PI5USB30213A_REG_CONTROL_PORT_SETTINGS_MASK;
	pi5usb30213a->regs.control |= src_pref;

	return pi5usb30213a_i2c_write_regs(pi5usb30213a);
}

static enum usb_role pi5usb30213a_get_attached_state(struct pi5usb30213a *pi5usb30213a)
{
	enum usb_role attached_state;

	switch (pi5usb30213a->regs.status & PI5USB30213A_REG_CC_STATUS_ATTACHED_STATUS_MASK) {
	case PI5USB30213A_REG_CC_STATUS_ATTACHED_STATUS_DEVICE:
		attached_state = USB_ROLE_HOST;
		break;
	case PI5USB30213A_REG_CC_STATUS_ATTACHED_STATUS_HOST:
		attached_state = USB_ROLE_DEVICE;
		break;
	default:
		attached_state = USB_ROLE_NONE;
		break;
	}

	return attached_state;
}

static int pi5usb30213a_dr_set(struct typec_port *port, enum typec_data_role role)
{
	struct pi5usb30213a *pi5usb30213a = typec_get_drvdata(port);
	enum usb_role role_val;
	int pref, ret = 0;

	if (role == TYPEC_HOST) {
		role_val = USB_ROLE_HOST;
		pref = PI5USB30213A_REG_CONTROL_PORT_SETTINGS_DRP_TRY_SRC;
	} else {
		role_val = USB_ROLE_DEVICE;
		pref = PI5USB30213A_REG_CONTROL_PORT_SETTINGS_DRP_TRY_SNK;
	}

	ret = pi5usb30213a_set_port_mode(pi5usb30213a, pref);
	usleep_range(10, 100);

	usb_role_switch_set_role(pi5usb30213a->role_sw, role_val);
	typec_set_data_role(pi5usb30213a->port, role);

	return ret;
}

static const struct typec_operations pi5usb30213a_ops = {
	.dr_set = pi5usb30213a_dr_set
};

static void pi5usb30213a_set_role(struct pi5usb30213a *pi5usb30213a)
{
	enum usb_role role_state = pi5usb30213a_get_attached_state(pi5usb30213a);

	dev_info(pi5usb30213a->dev, "attached state=%s\n", (role_state == USB_ROLE_NONE)?"NONE":(role_state == USB_ROLE_HOST)?"HOST":"DEVICE");

	usb_role_switch_set_role(pi5usb30213a->role_sw, role_state);
	if (role_state == USB_ROLE_NONE)
		pi5usb30213a_set_port_mode(pi5usb30213a,
				PI5USB30213A_REG_CONTROL_PORT_SETTINGS_DRP);

	switch (role_state) {
	case USB_ROLE_HOST:
		typec_set_data_role(pi5usb30213a->port, TYPEC_HOST);
		break;
	case USB_ROLE_DEVICE:
		typec_set_data_role(pi5usb30213a->port, TYPEC_DEVICE);
		break;
	default:
		break;
	}
}

static irqreturn_t pi5usb30213a_irq_handler(int irq, void *data)
{
	struct i2c_client *client = to_i2c_client(data);
	struct pi5usb30213a *pi5usb30213a = i2c_get_clientdata(client);

 	pi5usb30213a_i2c_read_regs(pi5usb30213a);

	pi5usb30213a_set_role(pi5usb30213a);

	return IRQ_HANDLED;
}

static int pi5usb30213a_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct typec_capability typec_cap = { };
	struct pi5usb30213a *pi5usb30213a;
	struct fwnode_handle *connector, *ep;
	int ret;

	pi5usb30213a = devm_kzalloc(&client->dev, sizeof(struct pi5usb30213a),
				 GFP_KERNEL);
	if (!pi5usb30213a)
		return -ENOMEM;

	i2c_set_clientdata(client, pi5usb30213a);

	pi5usb30213a->client = client;
	pi5usb30213a->dev = &client->dev;

	pi5usb30213a_i2c_read_regs(pi5usb30213a);

	pi5usb30213a_set_port_mode(pi5usb30213a,
				  PI5USB30213A_REG_CONTROL_PORT_SETTINGS_DRP);
	/* For backward compatibility check the connector child node first */
	connector = device_get_named_child_node(pi5usb30213a->dev, "connector");
	if (connector) {
		pi5usb30213a->role_sw = fwnode_usb_role_switch_get(connector);
	} else {
		ep = fwnode_graph_get_next_endpoint(dev_fwnode(pi5usb30213a->dev), NULL);
		if (!ep)
			return -ENODEV;
		connector = fwnode_graph_get_remote_port_parent(ep);
		fwnode_handle_put(ep);
		if (!connector)
			return -ENODEV;
		pi5usb30213a->role_sw = usb_role_switch_get(pi5usb30213a->dev);
	}

	if (IS_ERR(pi5usb30213a->role_sw)) {
		ret = PTR_ERR(pi5usb30213a->role_sw);
		goto err_put_fwnode;
	}

	typec_cap.prefer_role = TYPEC_NO_PREFERRED_ROLE;
	typec_cap.driver_data = pi5usb30213a;
	typec_cap.type = TYPEC_PORT_DRP;
	typec_cap.data = TYPEC_PORT_DRD;
	typec_cap.ops = &pi5usb30213a_ops;
	typec_cap.fwnode = connector;

	pi5usb30213a->port = typec_register_port(&client->dev, &typec_cap);
	if (IS_ERR(pi5usb30213a->port)) {
		ret = PTR_ERR(pi5usb30213a->port);
		goto err_put_role;
	}

	pi5usb30213a_set_role(pi5usb30213a);

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					pi5usb30213a_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"pi5usb30213a", &client->dev);
		if (ret)
			goto err_unreg_port;
	}

	fwnode_handle_put(connector);

	dev_info(&client->dev, "probed revision=0x%x\n", pi5usb30213a->regs.id >> 3);

	return 0;
err_unreg_port:
	typec_unregister_port(pi5usb30213a->port);
err_put_role:
	usb_role_switch_put(pi5usb30213a->role_sw);
err_put_fwnode:
	fwnode_handle_put(connector);

	return ret;
}

static int pi5usb30213a_remove(struct i2c_client *client)
{
	struct pi5usb30213a *pi5usb30213a = i2c_get_clientdata(client);

	typec_unregister_port(pi5usb30213a->port);
	usb_role_switch_put(pi5usb30213a->role_sw);

	return 0;
}

static const struct of_device_id dev_ids[] = {
	{ .compatible = "diodes,pi5usb30213a"},
	{}
};
MODULE_DEVICE_TABLE(of, dev_ids);

static struct i2c_driver pi5usb30213a_driver = {
	.driver = {
		.name = "pi5usb30213a",
		.of_match_table = of_match_ptr(dev_ids),
	},
	.probe = pi5usb30213a_probe,
	.remove =  pi5usb30213a_remove,
};

module_i2c_driver(pi5usb30213a_driver);

MODULE_AUTHOR("Arkadiusz Karas <arkadiusz.karas@somlabs.com>");
MODULE_DESCRIPTION("Diodes PI5USB30213A DRP Port Controller Driver");
MODULE_LICENSE("GPL");
