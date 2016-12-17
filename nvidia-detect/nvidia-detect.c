/*
 *  nvidia-detect - A utility to detect NVIDIA graphics cards
 *
 *  Copyright (C) 2013-2016 Philip J Perry <phil@elrepo.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pci/pci.h>
#include <linux/version.h>

#include "nvidia-detect.h"

#define PROGRAM_NAME		"nvidia-detect"
#define NVIDIA_VERSION		"375.26"

#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL	0x8086
#endif
#ifndef PCI_VENDOR_ID_NVIDIA
#define PCI_VENDOR_ID_NVIDIA	0x10de
#endif

/* Only recommend elrepo drivers on RHEL*/
#if (RHEL_MAJOR == 6 || RHEL_MAJOR == 7)
#define KMOD_NVIDIA			"kmod-nvidia"
#define KMOD_NVIDIA_340XX	"kmod-nvidia-340xx"
#define KMOD_NVIDIA_304XX	"kmod-nvidia-304xx"
#define KMOD_NVIDIA_173XX	""	/* No longer supported on RHEL */
#define KMOD_NVIDIA_96XX	""	/* No longer supported on RHEL */
#else	/* make no specific package recommendation */
#define KMOD_NVIDIA			""
#define KMOD_NVIDIA_340XX	""
#define KMOD_NVIDIA_304XX	""
#define KMOD_NVIDIA_173XX	""
#define KMOD_NVIDIA_96XX	""
#endif

/* 
 * Define the max Xorg Video Driver ABI supported by each NVIDIA driver
 * ABI_VIDEODRV_VERSION for Xorg is defined in:
 * http://cgit.freedesktop.org/xorg/xserver/tree/hw/xfree86/common/xf86Module.h
 */
#define XORG_ABI_CURRENT	23	/* 375.20; Xorg 1.19 */
#define XORG_ABI_96XX		12	/* 96.43.23 */
#define XORG_ABI_173XX		15	/* 173.14.39 */
#define XORG_ABI_304XX		20	/* 304.131; Xorg 1.18 */
#define XORG_ABI_340XX		20	/* 340.96; Xorg 1.18 */

/* Change the default Xorg log file here if it's different */
#define XORG_LOG_FILE	"/var/log/Xorg.0.log"

/* Define strings to search for in Xorg log file */
#define ABI_CLASS	"ABI class: X.Org Video Driver"
#define XORG_VID_DRV	"X.Org Video Driver:"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define STREQ(a, b) (strcmp ((a), (b)) == 0)

/* driver return codes */
enum {
	NVIDIA_NONE,
	NVIDIA_CURRENT,
	NVIDIA_LEGACY_96XX,
	NVIDIA_LEGACY_173XX,
	NVIDIA_LEGACY_304XX,
	NVIDIA_LEGACY_340XX,
};

static int ret = 0;
static char namebuf[128], *name;
static struct pci_access *pacc;
static struct pci_dev *dev;

/* command line options */
static bool opt_list = 0;
static bool opt_xorg = 0;
/* We can only return package names on RHEL */
#if (RHEL_MAJOR == 6 || RHEL_MAJOR == 7)
static bool opt_verbose = 0;
#else	/* do generic verbose output */
static bool opt_verbose = 1;
#endif

static struct option longopts[] = {
	/* { name  has_arg  *flag  val } */
	{"list",	0, 0, 'l'},
	{"verbose",	0, 0, 'v'},
	{"xorg",	0, 0, 'x'},
	{"help",	0, 0, 'h'},
	{"version",	0, 0, 'V'},
	{0, 0, 0, 0}
};

static void version(void)
{
	printf("Version: %s\n", NVIDIA_VERSION);
}

static void usage(void)
{
	printf("Usage: %s [-lvxhV]\n", PROGRAM_NAME);
	printf("  -l, --list         list all supported NVIDIA devices\n");
	printf("  -v, --verbose      verbose output\n");
	printf("  -x, --xorg         display xorg compatibility information\n");
	printf("  -h, --help         print this help and exit\n");
	printf("  -V, --version      display version number and exit\n\n");
	printf("Detect NVIDIA graphics cards and determine the correct NVIDIA driver.\n\n");
	printf("%s will return the following codes:\n\n", PROGRAM_NAME);
	printf("0: No supported devices found\n");
	printf("1: Device supported by the current %s NVIDIA driver\n", NVIDIA_VERSION);
	printf("2: Device supported by the legacy   96.xx NVIDIA driver\n");
	printf("3: Device supported by the legacy  173.xx NVIDIA driver\n");
	printf("4: Device supported by the legacy  304.xx NVIDIA driver\n");
	printf("5: Device supported by the legacy  340.xx NVIDIA driver\n\n");
	printf("Please report bugs at http://elrepo.org/bugs\n");
}

static void has_optimus(void)
{
	fprintf(stderr, "Optimus hardware detected: An Intel display controller was detected\n");
	fprintf(stderr, "Either disable the Intel display controller in the BIOS\n");
	fprintf(stderr, "or use the bumblebee driver to support Optimus hardware\n");
}

static void list_all_nvidia_devices(void)
{
	size_t i;

	printf("\n*** These devices are supported by the current %s NVIDIA "
		"driver %s ***\n\n", NVIDIA_VERSION, KMOD_NVIDIA);
	for (i = 0; i < ARRAY_SIZE(nv_current_pci_ids); i++) {
		name = pci_lookup_name(pacc, namebuf, sizeof(namebuf),
			PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			PCI_VENDOR_ID_NVIDIA, nv_current_pci_ids[i]);

		printf("[10de:%04x] %s\n", nv_current_pci_ids[i], name);
	}

	printf("\n*** These devices are supported by the legacy 340.xx NVIDIA "
		"driver %s ***\n\n", KMOD_NVIDIA_340XX);
	for (i = 0; i < ARRAY_SIZE(nv_340xx_pci_ids); i++) {
		name = pci_lookup_name(pacc, namebuf, sizeof(namebuf),
			PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			PCI_VENDOR_ID_NVIDIA, nv_340xx_pci_ids[i]);

		printf("[10de:%04x] %s\n", nv_340xx_pci_ids[i], name);
	}

	printf("\n*** These devices are supported by the legacy 304.xx NVIDIA "
		"driver %s ***\n\n", KMOD_NVIDIA_304XX);
	for (i = 0; i < ARRAY_SIZE(nv_304xx_pci_ids); i++) {
		name = pci_lookup_name(pacc, namebuf, sizeof(namebuf),
			PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			PCI_VENDOR_ID_NVIDIA, nv_304xx_pci_ids[i]);

		printf("[10de:%04x] %s\n", nv_304xx_pci_ids[i], name);
	}

	printf("\n*** These devices are supported by the legacy 173.xx NVIDIA "
		"driver %s ***\n\n", KMOD_NVIDIA_173XX);
	for (i = 0; i < ARRAY_SIZE(nv_173xx_pci_ids); i++) {
		name = pci_lookup_name(pacc, namebuf, sizeof(namebuf),
			PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			PCI_VENDOR_ID_NVIDIA, nv_173xx_pci_ids[i]);

		printf("[10de:%04x] %s\n", nv_173xx_pci_ids[i], name);
	}

	printf("\n*** These devices are supported by the legacy 96.xx NVIDIA "
		"driver %s ***\n\n", KMOD_NVIDIA_96XX);
	for (i = 0; i < ARRAY_SIZE(nv_96xx_pci_ids); i++) {
		name = pci_lookup_name(pacc, namebuf, sizeof(namebuf),
			PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
			PCI_VENDOR_ID_NVIDIA, nv_96xx_pci_ids[i]);

		printf("[10de:%04x] %s\n", nv_96xx_pci_ids[i], name);
	}
}

static int nv_lookup_device_id(u_int16_t device_id)
{
	size_t i;

	/** Find devices supported by the current driver **/
	for (i = 0; i < ARRAY_SIZE(nv_current_pci_ids); i++) {
		if (device_id == nv_current_pci_ids[i]) {
			if (opt_verbose) {
				printf("This device requires the current %s NVIDIA "
					"driver %s\n", NVIDIA_VERSION, KMOD_NVIDIA);
			}
			return NVIDIA_CURRENT;
		}
	}

	/** Find devices supported by the 340xx legacy driver **/
	for (i = 0; i < ARRAY_SIZE(nv_340xx_pci_ids); i++) {
		if (device_id == nv_340xx_pci_ids[i]) {
			if (opt_verbose) {
				printf("This device requires the legacy 340.xx NVIDIA "
					"driver %s\n", KMOD_NVIDIA_340XX);
			}
			return NVIDIA_LEGACY_340XX;
		}
	}

	/** Find devices supported by the 304xx legacy driver **/
	for (i = 0; i < ARRAY_SIZE(nv_304xx_pci_ids); i++) {
		if (device_id == nv_304xx_pci_ids[i]) {
			if (opt_verbose) {
				printf("This device requires the legacy 304.xx NVIDIA "
					"driver %s\n", KMOD_NVIDIA_304XX);
			}
			return NVIDIA_LEGACY_304XX;
		}
	}

	/** Find devices supported by the 173xx legacy driver **/
	for (i = 0; i < ARRAY_SIZE(nv_173xx_pci_ids); i++) {
		if (device_id == nv_173xx_pci_ids[i]) {
			if (opt_verbose) {
				printf("This device requires the legacy 173.xx NVIDIA "
					"driver %s\n", KMOD_NVIDIA_173XX);
			}
			return NVIDIA_LEGACY_173XX;
		}
	}

	/** Find devices supported by the 96xx legacy driver **/
	for (i = 0; i < ARRAY_SIZE(nv_96xx_pci_ids); i++) {
		if (device_id == nv_96xx_pci_ids[i]) {
			if (opt_verbose) {
				printf("This device requires the legacy 96.xx NVIDIA "
					"driver %s\n", KMOD_NVIDIA_96XX);
			}
			return NVIDIA_LEGACY_96XX;
		}
	}

	/** Catch NVIDIA devices that aren't supported **/
	fprintf(stderr, "This device does not appear to be supported at present\n");
	fprintf(stderr, "Please report at http://elrepo.org/bugs quoting the output "
			"from '/sbin/lspci -nn'\n");

	return NVIDIA_NONE;
}

static int terse_output(void)
{
	if (ret == NVIDIA_CURRENT) {
		printf("%s\n", KMOD_NVIDIA);
		return 0;
	}
	else if (ret == NVIDIA_LEGACY_340XX) {
		printf("%s\n", KMOD_NVIDIA_340XX);
		return 0;
	}
	else if (ret == NVIDIA_LEGACY_304XX) {
		printf("%s\n", KMOD_NVIDIA_304XX);
		return 0;
	} else {
	return 0;
	}
}

static int get_xorg_abi(void)
{
	FILE *fp;
	char line[128];
	char *ret;
	int version = 0;

	if ((fp = fopen(XORG_LOG_FILE, "r")) == NULL) {
		fprintf(stderr, "WARNING: Xorg log file %s does not exist\n", XORG_LOG_FILE);
		fprintf(stderr, "WARNING: Unable to determine Xorg ABI compatibility\n");

		return version;
	}

	while (( fgets(line, sizeof(line), fp) ) != NULL) {
		/* 
		 * Not all strings are present in all Xorg log files so we need to
		 * look for matches against multiple strings until we find a match.
		 */
		if ((ret = strstr(line, XORG_VID_DRV)) != NULL)
			if ((sscanf(ret, "X.Org Video Driver: %d", &version)) == 1)
				break;

		if ((ret = strstr(line, ABI_CLASS)) != NULL)
			if ((sscanf(ret, "ABI class: X.Org Video Driver, version %d",
					&version)) == 1)
				break;
	}

	if (version && opt_xorg)
		printf("Xorg Video Driver ABI detected: %d\n", version);

	fclose(fp);

	return version;
}

static bool check_xorg_abi_compat(int driver)
{
	int abi = 0;

	abi = get_xorg_abi();

	if (abi > 0) {
		if (driver == NVIDIA_CURRENT && abi <= XORG_ABI_CURRENT )
			return 1;
		else if (driver == NVIDIA_LEGACY_96XX && abi <= XORG_ABI_96XX )
			return 1;
		else if (driver == NVIDIA_LEGACY_173XX && abi <= XORG_ABI_173XX )
			return 1;
		else if (driver == NVIDIA_LEGACY_304XX && abi <= XORG_ABI_304XX )
			return 1;
		else if (driver == NVIDIA_LEGACY_340XX && abi <= XORG_ABI_340XX )
			return 1;
		else
			return 0;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	bool has_intel = 0;
	bool has_nvidia = 0;
	bool abi_compat = 0;
	int c = 0;

	while ((c = getopt_long(argc, argv, "lvxVh", longopts, 0)) != EOF)
		switch (c) {
		case 'l':
			opt_list = true;
			break;
		case 'v':
			opt_verbose = true;
			break;
		case 'x':
			opt_xorg = true;
			break;
		case 'V':
			version();
			exit(0);
		case 'h':
			usage();
			exit(0);
		default:
			usage();
			exit(0);
		}

	pacc = pci_alloc();		/* Get the pci_access structure */
	pci_init(pacc);			/* Initialize the PCI library */

	if (opt_list) {
		list_all_nvidia_devices();
		goto exit;
	}

	pci_scan_bus(pacc);		/* Scan the bus for devices */

	if (opt_verbose) {
		printf("Probing for supported NVIDIA devices...\n");
	}

	/* Iterate over all devices */
	for (dev=pacc->devices; dev; dev=dev->next) {

		if (!dev->device_class) {
			fprintf(stderr, "Error getting device_class\n");
			ret = -1;
			goto exit;
		}

		if ((dev->device_class & 0xff00) == 0x0300) {

			/* Get the name of the device */
			name = pci_lookup_name(pacc, namebuf, sizeof(namebuf),
				PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
				dev->vendor_id, dev->device_id);

			if (opt_verbose) {
				printf("[%04x:%04x] %s\n",
					dev->vendor_id, dev->device_id, name);
			}

			/* Find NVIDIA device */
			if (dev->vendor_id == PCI_VENDOR_ID_NVIDIA) {
				has_nvidia = true;
				ret = nv_lookup_device_id(dev->device_id);
			}

			/* 
			 * Find Intel device for simplistic detection
			 * of Optimus hardware configurations
			 */
			if (dev->vendor_id == PCI_VENDOR_ID_INTEL)
				has_intel = true;

		}	/* End of device_class */

	}		/* End iteration of devices */

	/* Print package name */
	if (!opt_verbose) {
		terse_output();
	}

	/* Check Xorg ABI compatibility */
	if (ret > 0) {
		if (opt_xorg)
			printf("\nChecking ABI compatibility with Xorg Server...\n");

		abi_compat = check_xorg_abi_compat(ret);

		if (!abi_compat)
			fprintf(stderr, "WARNING: The driver for this device "
			"does not support the current Xorg version\n");
		else
			if (opt_xorg)
				printf("ABI compatibility check passed\n"); 
	}

	/* Check for Optimus hardware */
	if (has_intel && has_nvidia)
		has_optimus();

	/* Catch cases where no NVIDIA devices were detected */
	if (!has_nvidia)
		fprintf(stderr, "No NVIDIA devices were found.\n");

exit:
	pci_cleanup(pacc);	/* Close everything */

	exit(ret);
}
