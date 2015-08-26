#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/interrupt.h>


#define DEMO_VENDOR_ID   	PCI_ANY_ID
#define DEMO_DEVICE_ID   	PCI_ANY_ID
#define DRVER_NAME              "pcidemo"
#define DRVER_CLASS_NAME        "pcidevice"         
#define DEMO_CDEV_MAX_DEVICE   (1)
#define DEMO_CDEV_MAJOR        (0)
#define DEMO_NR_DEVS           (1)
#define DEMO_BAR_NUM           (6)
#define DEMO_IOCTL_PING        0x20001

static dev_t demo_devno;
static int demo_cdev_major = DEMO_CDEV_MAJOR;
static int demo_cdev_minor = 0;
static int demo_nr_devs    = DEMO_NR_DEVS;

struct demo_dev {
	struct pci_dev	*pdev;
	struct cdev cdev;
	dev_t devno;
	
	void * __iomem bar_va[DEMO_BAR_NUM];
	phys_addr_t bar_pa[DEMO_BAR_NUM];
	unsigned long bar_len[DEMO_BAR_NUM];
	
	int irq;
	int msi_enabled;
	int in_use;
	int got_regions;
	struct class *drv_cls;
};

static ssize_t demo_read(struct file *filp, char __user *buf, size_t count,
				loff_t *f_pos)
{
	return 0;
}
static ssize_t demo_write(struct file *filp, const char __user *buf, size_t count,
				loff_t *f_pos)
{
	return 0;
}

static int demo_open(struct inode *inode, struct file *filp)
{
	struct demo_dev *priv;

	priv = container_of(inode->i_cdev, struct demo_dev, cdev);
	filp->private_data = priv;

	return 0;
}

static int demo_release(struct inode *inode, struct file *filp)
{
	
	return 0;
}

static long demo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	//struct demo_dev *priv = filp->private_data;
	int ret = 0;
	char ping_str[20] = "hello from demo\n";
	
	switch(cmd)
	{
		case DEMO_IOCTL_PING:	
			ret = copy_to_user((char __user *)arg, ping_str, strlen(ping_str)+1);			
			break;
		
		default:
			pr_err("Wrong ioctl cmd\n");
			return -ENOTTY;
			
	}
	
	return ret;	
}

static irqreturn_t demo_isr(int irq, void *dev_id)
{
	struct demo_dev *priv = (struct demo_dev *)dev_id;
	if (!priv)
		return IRQ_NONE;

	// TODO: implement your isr here

	return IRQ_HANDLED;
}

static struct file_operations demo_fops = {
	.owner          = THIS_MODULE,
	.read           = demo_read,
	.write          = demo_write,
	.unlocked_ioctl = demo_ioctl,
	.open           = demo_open,
	.release        = demo_release,
};

static int demo_cdev_init(struct demo_dev *demodev)
{
	int ret;

	ret = alloc_chrdev_region(&demo_devno, 0, demo_nr_devs, DRVER_NAME);
	if(ret < 0)
	{	
		pr_err("Can't get cdev major\n");
		return ret;
	}
	
	demo_cdev_major = MAJOR(demo_devno);
	demo_devno = MKDEV(demo_cdev_major, demo_cdev_minor);
	cdev_init(&demodev->cdev, &demo_fops);
	demodev->cdev.owner = THIS_MODULE;
	demodev->cdev.ops = &demo_fops;
	
	ret = cdev_add(&demodev->cdev, demo_devno, 1);
	demodev->devno = demo_devno;
	if(ret < 0)
	{
		pr_err("Can't add demo cdev\n");	
	}

	return ret;
	
}

static void demo_unmap_bars(struct demo_dev *priv, struct pci_dev *pdev)
{
	int i;
	for (i = 0; i < DEMO_BAR_NUM; i++) 
	{
		/* is this BAR mapped? */
		if (priv->bar_va[i])
		{
			/* unmap BAR */
			pci_iounmap(pdev, priv->bar_va[i]);
			priv->bar_va[i] = NULL;
		}
	}
}

static int demo_map_bars(struct demo_dev *priv, struct pci_dev *pdev)
{
	int rc = 0;
	int i;

	resource_size_t bar_start, bar_end, bar_length;
	
	/* iterate through all the BARs */
	for (i = 0; i < DEMO_BAR_NUM; i++)
	{
		bar_start = pci_resource_start(pdev, i);
		bar_end = pci_resource_end(pdev, i);

		priv->bar_va[i] = NULL;
	
		/* do not map BARs with address 0 */
		if (!bar_start || !bar_end) 
		{
			pr_warning("BAR #%d is not present\n", i);
			rc = -1;
			continue;
		}
		
		bar_length = pci_resource_len(pdev, i);

		/* map the device memory or IO region into kernel virtual address space */
		priv->bar_va[i] = pci_iomap(pdev, i, bar_length);
		priv->bar_pa[i] = bar_start;
		priv->bar_len[i] = bar_length;
		if (!priv->bar_va[i]) 
		{
			pr_err("Can't map BAR #%d.\n", i);
			rc = -1;
			goto fail;
		}
		pr_info("BAR[%d] 0x%pa mapped at 0x%p length %lu.\n", i, &priv->bar_pa[i], priv->bar_va[i], (unsigned long)bar_length);
	}
		
	/* succesfully mapped all required BAR regions */
	rc = 0;
	goto success;
fail:
	/* unmap any BARs that we did map */
	demo_unmap_bars(priv, pdev);
success:
	return rc;
}

static int demo_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	struct demo_dev *priv = NULL;
	int rc = -ENODEV;
	 
	priv = kzalloc(sizeof(struct demo_dev), GFP_KERNEL);
	if(!priv)
	{
		return -ENOMEM;		
	}

	priv->pdev = pdev;
	pci_set_drvdata(pdev, priv); 

	/* enable device */
	if(pci_enable_device(pdev))
	{
		pr_err("%s can't enable device\n", pci_name(pdev));
		goto err_out;
	}

	/* enable bus master capability on device */
	pci_set_master(pdev);
	
	rc = pci_enable_msi(pdev);
	if(rc)
	{
		pr_info("Can't enable MSI interrupting\n");
		priv->msi_enabled = 0;
	}
	else
	{
		pr_info("Enabled MSI interrupting\n");
		priv->msi_enabled = 1;
	}
    
	rc = pci_request_regions(pdev, DRVER_NAME);
	if(rc)
	{
		priv->in_use = 1;
		pr_err("pci request regions fail\n");
		goto err_out;
	}
	priv->got_regions = 1;

	if(!pci_set_dma_mask(pdev, DMA_BIT_MASK(64)))
	{
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		pr_info("Using a 64-bit DMA mask\n");
	}
	else
	{
		if(!pci_set_dma_mask(pdev, DMA_BIT_MASK(32)))
		{
			pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
			pr_info("Using a 32-bit DMA mask\n");
		}
		else
		{
			pr_err("No suitable DMA possible\n");
			rc = -1;
			goto err_out;
		}
	}

	priv->irq = pdev->irq;
	pr_info("IRQ is %d\n", pdev->irq);

	rc = request_irq(pdev->irq, demo_isr, IRQF_SHARED, DRVER_NAME, (void *)priv);
	if(rc)
	{
		pr_err("IRQ #%d, error %d\n", priv->irq, rc);
		goto err_irq;
	}

	demo_map_bars(priv, pdev);

	rc = demo_cdev_init(priv);
	if(rc < 0)
		goto err_cdev_init;

	priv->drv_cls = class_create(THIS_MODULE, DRVER_CLASS_NAME);
	if(IS_ERR(priv->drv_cls)) 
	{
		pr_err("Creating class fail\n");
		goto err_class_create;
	}
	device_create(priv->drv_cls, NULL, priv->devno, NULL, DRVER_NAME "%d", MINOR(priv->devno));

	return 0;

err_class_create:
err_cdev_init:

err_irq:
	if(priv->msi_enabled)
		pci_disable_msi(pdev);
	if(!priv->in_use)
		pci_disable_device(pdev);
	if(priv->got_regions)
		pci_release_regions(pdev);
	
err_out:
	if(priv)
	{
		kfree(priv);
	}
	
	return rc;
}

static void demo_remove(struct pci_dev *pdev)
{
	struct demo_dev *priv;
	priv = (struct demo_dev*)pci_get_drvdata(pdev);
 
	cdev_del(&priv->cdev);
	unregister_chrdev_region(priv->devno, demo_nr_devs);
	device_destroy(priv->drv_cls, priv->devno);  
	class_destroy(priv->drv_cls);

	if(priv->irq >= 0)
	{
		free_irq(priv->irq, (void*)priv);
	}
	
	if(priv->msi_enabled)
	{
		pci_disable_msi(pdev);
		priv->msi_enabled = 0;
	}

	demo_unmap_bars(priv, pdev);

	pci_release_regions(pdev);
	priv->pdev = NULL;
	kfree(priv);

	pci_set_drvdata(pdev,NULL);
	pci_disable_device(pdev);
}

static struct pci_device_id demo_pci_tbl[] = {
	{DEMO_VENDOR_ID, DEMO_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0, }
};
MODULE_DEVICE_TABLE (pci, demo_pci_tbl);

static struct pci_driver demo_pci_driver = {
	.name     = DRVER_NAME,
	.id_table = demo_pci_tbl,
	.probe    = demo_probe,
	.remove   = demo_remove,
};

static int __init demo_init_module(void)
{
	return pci_register_driver(&demo_pci_driver);
}

static void __exit demo_cleanup_module(void)
{
	pci_unregister_driver(&demo_pci_driver);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Jie");
MODULE_DESCRIPTION ("Linux PCI Device Driver Demo");

module_init(demo_init_module);
module_exit(demo_cleanup_module);
