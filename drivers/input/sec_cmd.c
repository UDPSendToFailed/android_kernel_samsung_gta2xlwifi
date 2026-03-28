/*
 * sec_cmd.c - samsung factory command driver
 *
 * Copyright (C) 2014 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/input/sec_cmd.h>

#if defined(USE_SEC_CMD_QUEUE)
static void sec_cmd_store_function(struct sec_cmd_data *data);
#endif

void sec_cmd_set_cmd_exit(struct sec_cmd_data *data)
{
	mutex_lock(&data->cmd_lock);
	data->cmd_is_running = false;
	mutex_unlock(&data->cmd_lock);

#ifdef USE_SEC_CMD_QUEUE
	mutex_lock(&data->fifo_lock);
	if (kfifo_len(&data->cmd_queue)) {
		pr_info("%s %s: do next cmd, left cmd[%d]\n", SECLOG, __func__,
			(int)(kfifo_len(&data->cmd_queue) / sizeof(struct command)));
		mutex_unlock(&data->fifo_lock);

		mutex_lock(&data->cmd_lock);
		data->cmd_is_running = true;
		mutex_unlock(&data->cmd_lock);

		data->cmd_state = SEC_CMD_STATUS_RUNNING;
		schedule_work(&data->cmd_work.work);
	} else {
		mutex_unlock(&data->fifo_lock);
	}
#endif
}

#if defined(USE_SEC_CMD_QUEUE)
static void cmd_exit_work(struct work_struct *work)
{
	struct sec_cmd_data *data =
		container_of(work, struct sec_cmd_data, cmd_work.work);

	sec_cmd_store_function(data);
}
#endif

void sec_cmd_set_default_result(struct sec_cmd_data *data)
{
	memset(data->cmd_result, 0x00, SEC_CMD_RESULT_STR_LEN_EXPAND);
	memcpy(data->cmd_result, data->cmd, SEC_CMD_STR_LEN);
	strlcat(data->cmd_result, ":", SEC_CMD_RESULT_STR_LEN_EXPAND);
}

void sec_cmd_set_cmd_result_all(struct sec_cmd_data *data, char *buff, int len,
	char *item)
{
	size_t cmd_result_len;

	cmd_result_len = strlen(data->cmd_result_all) + len + 2 + strlen(item);
	if (cmd_result_len >= (unsigned int)SEC_CMD_RESULT_STR_LEN) {
		pr_err("%s %s: cmd length is over (%d)!!", SECLOG, __func__,
			(int)cmd_result_len);
		return;
	}

	data->item_count++;
	strlcat(data->cmd_result_all, " ", SEC_CMD_RESULT_STR_LEN);
	strlcat(data->cmd_result_all, item, SEC_CMD_RESULT_STR_LEN);
	strlcat(data->cmd_result_all, ":", SEC_CMD_RESULT_STR_LEN);
	strlcat(data->cmd_result_all, buff, SEC_CMD_RESULT_STR_LEN);
}

void sec_cmd_set_cmd_result(struct sec_cmd_data *data, char *buff, int len)
{
	if (strlen(buff) >= (unsigned int)SEC_CMD_RESULT_STR_LEN_EXPAND) {
		pr_err("%s %s: cmd length is over (%d)!!", SECLOG, __func__,
			(int)strlen(buff));
		strlcat(data->cmd_result, "NG", SEC_CMD_RESULT_STR_LEN_EXPAND);
		return;
	}

	strlcat(data->cmd_result, buff, SEC_CMD_RESULT_STR_LEN_EXPAND);
}

#ifndef USE_SEC_CMD_QUEUE
static ssize_t sec_cmd_store(struct device *dev, struct device_attribute *devattr,
	const char *buf, size_t count)
{
	struct sec_cmd_data *data = dev_get_drvdata(dev);
	char *cur, *start, *end;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int len, index;
	struct sec_cmd *sec_cmd_ptr = NULL;
	char delim = ',';
	bool cmd_found = false;
	int param_cnt = 0;

	if (!data) {
		pr_err("%s %s: No platform data found\n", SECLOG, __func__);
		return -EINVAL;
	}

	if (strnlen(buf, SEC_CMD_STR_LEN) >= SEC_CMD_STR_LEN ||
	    count >= (unsigned int)SEC_CMD_STR_LEN) {
		pr_err("%s %s: cmd length is over (%s)!!\n", SECLOG, __func__, buf);
		return -EINVAL;
	}

	if (data->cmd_is_running) {
		pr_err("%s %s: other cmd is running.\n", SECLOG, __func__);
		return -EBUSY;
	}

	mutex_lock(&data->cmd_lock);
	data->cmd_is_running = true;
	mutex_unlock(&data->cmd_lock);

	data->cmd_state = SEC_CMD_STATUS_RUNNING;
	for (index = 0; index < ARRAY_SIZE(data->cmd_param); index++)
		data->cmd_param[index] = 0;

	len = (int)count;
	if (*(buf + len - 1) == '\n')
		len--;

	memset(data->cmd, 0x00, ARRAY_SIZE(data->cmd));
	memcpy(data->cmd, buf, len);

	cur = strchr(buf, (int)delim);
	if (cur)
		memcpy(buff, buf, cur - buf);
	else
		memcpy(buff, buf, len);

	pr_debug("%s %s: COMMAND = %s\n", SECLOG, __func__, buff);

	list_for_each_entry(sec_cmd_ptr, &data->cmd_list_head, list) {
		if (!strncmp(buff, sec_cmd_ptr->cmd_name, SEC_CMD_STR_LEN)) {
			cmd_found = true;
			break;
		}
	}

	if (!cmd_found) {
		list_for_each_entry(sec_cmd_ptr, &data->cmd_list_head, list) {
			if (!strncmp("not_support_cmd", sec_cmd_ptr->cmd_name,
				SEC_CMD_STR_LEN))
				break;
		}
	}

	if (cur && cmd_found) {
		cur++;
		start = cur;
		memset(buff, 0x00, ARRAY_SIZE(buff));

		do {
			if (*cur == delim || cur - buf == len) {
				end = cur;
				memcpy(buff, start, end - start);
				*(buff + strnlen(buff, ARRAY_SIZE(buff))) = '\0';
				if (kstrtoint(buff, 10,
					data->cmd_param + param_cnt) < 0)
					goto err_out;
				start = cur + 1;
				memset(buff, 0x00, ARRAY_SIZE(buff));
				param_cnt++;
			}
			cur++;
		} while ((cur - buf <= len) && (param_cnt < SEC_CMD_PARAM_NUM));
	}

	if (cmd_found) {
		pr_info("%s %s: cmd = %s", SECLOG, __func__,
			sec_cmd_ptr->cmd_name);
		for (index = 0; index < param_cnt; index++) {
			if (index == 0)
				pr_cont(" param =");
			pr_cont(" %d", data->cmd_param[index]);
		}
		pr_cont("\n");
	} else {
		pr_info("%s %s: cmd = %s(%s)\n", SECLOG, __func__, buff,
			sec_cmd_ptr->cmd_name);
	}

	sec_cmd_ptr->cmd_func(data);

err_out:
	return count;
}
#else
static void sec_cmd_store_function(struct sec_cmd_data *data)
{
	char *cur, *start, *end;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int len, index;
	struct sec_cmd *sec_cmd_ptr = NULL;
	char delim = ',';
	bool cmd_found = false;
	int param_cnt = 0;
	int ret;
	const char *buf;
	size_t count;
	struct command cmd = {{0}};

	if (!data) {
		pr_err("%s %s: No platform data found\n", SECLOG, __func__);
		return;
	}

	mutex_lock(&data->fifo_lock);
	if (kfifo_len(&data->cmd_queue)) {
		ret = kfifo_out(&data->cmd_queue, &cmd, sizeof(struct command));
		if (!ret) {
			pr_err("%s %s: kfifo_out failed, it seems empty, ret=%d\n",
				SECLOG, __func__, ret);
			mutex_unlock(&data->fifo_lock);
			return;
		}
	} else {
		pr_err("%s %s: left cmd is nothing\n", SECLOG, __func__);
		mutex_unlock(&data->fifo_lock);
		return;
	}
	mutex_unlock(&data->fifo_lock);

	buf = cmd.cmd;
	count = strlen(cmd.cmd);
	for (index = 0; index < ARRAY_SIZE(data->cmd_param); index++)
		data->cmd_param[index] = 0;

	len = (int)count;
	if (*(buf + len - 1) == '\n')
		len--;

	memset(data->cmd, 0x00, ARRAY_SIZE(data->cmd));
	memcpy(data->cmd, buf, len);

	cur = strchr(buf, (int)delim);
	if (cur)
		memcpy(buff, buf, cur - buf);
	else
		memcpy(buff, buf, len);

	pr_info("%s %s: COMMAND = %s\n", SECLOG, __func__, buff);

	list_for_each_entry(sec_cmd_ptr, &data->cmd_list_head, list) {
		if (!strncmp(buff, sec_cmd_ptr->cmd_name, SEC_CMD_STR_LEN)) {
			cmd_found = true;
			break;
		}
	}

	if (!cmd_found) {
		list_for_each_entry(sec_cmd_ptr, &data->cmd_list_head, list) {
			if (!strncmp("not_support_cmd", sec_cmd_ptr->cmd_name,
				SEC_CMD_STR_LEN))
				break;
		}
	}

	if (cur && cmd_found) {
		cur++;
		start = cur;
		memset(buff, 0x00, ARRAY_SIZE(buff));

		do {
			if (*cur == delim || cur - buf == len) {
				end = cur;
				memcpy(buff, start, end - start);
				*(buff + strnlen(buff, ARRAY_SIZE(buff))) = '\0';
				if (kstrtoint(buff, 10,
					data->cmd_param + param_cnt) < 0)
					break;
				start = cur + 1;
				memset(buff, 0x00, ARRAY_SIZE(buff));
				param_cnt++;
			}
			cur++;
		} while ((cur - buf <= len) && (param_cnt < SEC_CMD_PARAM_NUM));
	}

	if (cmd_found) {
		pr_info("%s %s: cmd = %s", SECLOG, __func__, sec_cmd_ptr->cmd_name);
		for (index = 0; index < param_cnt; index++) {
			if (index == 0)
				pr_cont(" param =");
			pr_cont(" %d", data->cmd_param[index]);
		}
		pr_cont("\n");
	} else {
		pr_info("%s %s: cmd = %s(%s)\n", SECLOG, __func__, buff,
			sec_cmd_ptr->cmd_name);
	}

	sec_cmd_ptr->cmd_func(data);
}

static ssize_t sec_cmd_store(struct device *dev, struct device_attribute *devattr,
	const char *buf, size_t count)
{
	struct sec_cmd_data *data = dev_get_drvdata(dev);
	struct command cmd = {{0}};
	int ret;

	if (!data) {
		pr_err("%s %s: No platform data found\n", SECLOG, __func__);
		return -EINVAL;
	}

	if (data->cmd_is_running) {
		mutex_lock(&data->fifo_lock);
		if (kfifo_len(&data->cmd_queue) >= SEC_CMD_MAX_QUEUE * sizeof(cmd)) {
			pr_err("%s %s: cmd_queue is full!!\n", SECLOG, __func__);
			mutex_unlock(&data->fifo_lock);
			return -ENOSPC;
		}
		ret = kfifo_in(&data->cmd_queue, &cmd, sizeof(struct command));
		if (!ret) {
			pr_err("%s %s: kfifo_in failed, ret=%d\n", SECLOG, __func__, ret);
			mutex_unlock(&data->fifo_lock);
			return -ENOSPC;
		}
		strlcpy(cmd.cmd, buf, sizeof(cmd.cmd));
		ret = kfifo_in(&data->cmd_queue, &cmd, sizeof(struct command));
		pr_info("%s %s: push cmd: %s, left cmd[%d]\n", SECLOG, __func__,
			cmd.cmd,
			(int)(kfifo_len(&data->cmd_queue) / sizeof(struct command)));
		mutex_unlock(&data->fifo_lock);
		return count;
	}

	mutex_lock(&data->cmd_lock);
	data->cmd_is_running = true;
	mutex_unlock(&data->cmd_lock);
	data->cmd_state = SEC_CMD_STATUS_RUNNING;
	strlcpy(cmd.cmd, buf, sizeof(cmd.cmd));
	mutex_lock(&data->fifo_lock);
	ret = kfifo_in(&data->cmd_queue, &cmd, sizeof(struct command));
	mutex_unlock(&data->fifo_lock);
	if (!ret) {
		pr_err("%s %s: kfifo_in failed, ret=%d\n", SECLOG, __func__, ret);
		return -ENOSPC;
	}
	schedule_work(&data->cmd_work.work);
	return count;
}
#endif

static ssize_t sec_cmd_show(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct sec_cmd_data *data = dev_get_drvdata(dev);

	if (!data) {
		pr_err("%s %s: No platform data found\n", SECLOG, __func__);
		return -EINVAL;
	}

	pr_info("%s %s: \"%s(%d)\"\n", SECLOG, __func__,
		data->cmd_result, strlen(data->cmd_result));
	data->cmd_state = SEC_CMD_STATUS_WAITING;
	return snprintf(buf, SEC_CMD_RESULT_STR_LEN_EXPAND, "%s\n",
		data->cmd_result);
}

static ssize_t sec_cmd_status_show(struct device *dev,
	struct device_attribute *devattr, char *buf)
{
	struct sec_cmd_data *data = dev_get_drvdata(dev);
	char buff[16] = { 0 };

	if (!data) {
		pr_err("%s %s: No platform data found\n", SECLOG, __func__);
		return -EINVAL;
	}

	switch (data->cmd_state) {
	case SEC_CMD_STATUS_WAITING:
		strlcpy(buff, "WAITING", sizeof(buff));
		break;
	case SEC_CMD_STATUS_RUNNING:
		strlcpy(buff, "RUNNING", sizeof(buff));
		break;
	case SEC_CMD_STATUS_OK:
		strlcpy(buff, "OK", sizeof(buff));
		break;
	case SEC_CMD_STATUS_FAIL:
		strlcpy(buff, "FAIL", sizeof(buff));
		break;
	case SEC_CMD_STATUS_NOT_APPLICABLE:
		strlcpy(buff, "NOT_APPLICABLE", sizeof(buff));
		break;
	default:
		break;
	}

	return snprintf(buf, SEC_CMD_STR_LEN, "%s\n", buff);
}

static ssize_t sec_cmd_list_show(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct sec_cmd_data *data = dev_get_drvdata(dev);
	struct sec_cmd *sec_cmd_ptr = NULL;
	char *buff = buf;

	if (!data) {
		pr_err("%s %s: No platform data found\n", SECLOG, __func__);
		return -EINVAL;
	}

	buff += snprintf(buff, PAGE_SIZE, "++factory command list++\n");
	list_for_each_entry(sec_cmd_ptr, &data->cmd_list_head, list)
		buff += snprintf(buff, PAGE_SIZE - (buff - buf), "%s\n",
			sec_cmd_ptr->cmd_name);

	return strlen(buf);
}

static DEVICE_ATTR(cmd, S_IWUSR | S_IWGRP, sec_cmd_show, sec_cmd_store);
static DEVICE_ATTR(cmd_status, S_IRUGO, sec_cmd_status_show, NULL);
static DEVICE_ATTR(cmd_list, S_IRUGO, sec_cmd_list_show, NULL);

static struct attribute *sec_fac_attrs[] = {
	&dev_attr_cmd.attr,
	&dev_attr_cmd_status.attr,
	&dev_attr_cmd_list.attr,
	NULL,
};

static struct attribute_group sec_fac_attr_group = {
	.attrs = sec_fac_attrs,
};

int sec_cmd_init(struct sec_cmd_data *data, struct sec_cmd *cmds,
	int len, int devt)
{
	const char *dev_name;
	int ret, index;

	INIT_LIST_HEAD(&data->cmd_list_head);

	data->cmd_buffer_size = 0;
	for (index = 0; index < len; index++) {
		list_add_tail(&cmds[index].list, &data->cmd_list_head);
		if (cmds[index].cmd_name)
			data->cmd_buffer_size += strlen(cmds[index].cmd_name) + 1;
	}

	mutex_init(&data->cmd_lock);
	mutex_lock(&data->cmd_lock);
	data->cmd_is_running = false;
	mutex_unlock(&data->cmd_lock);

	data->cmd_result = kzalloc(SEC_CMD_RESULT_STR_LEN_EXPAND, GFP_KERNEL);
	if (!data->cmd_result)
		goto err_alloc_cmd_result;

#ifdef USE_SEC_CMD_QUEUE
	if (kfifo_alloc(&data->cmd_queue,
		SEC_CMD_MAX_QUEUE * sizeof(struct command), GFP_KERNEL)) {
		pr_err("%s %s: failed to alloc queue for cmd\n", SECLOG, __func__);
		goto err_alloc_queue;
	}
	mutex_init(&data->fifo_lock);
	INIT_DELAYED_WORK(&data->cmd_work, cmd_exit_work);
#endif

	if (devt == SEC_CLASS_DEVT_TSP)
		dev_name = SEC_CLASS_DEV_NAME_TSP;
	else if (devt == SEC_CLASS_DEVT_TKEY)
		dev_name = SEC_CLASS_DEV_NAME_TKEY;
	else if (devt == SEC_CLASS_DEVT_WACOM)
		dev_name = SEC_CLASS_DEV_NAME_WACOM;
	else {
		pr_err("%s %s: not defined devt=%d\n", SECLOG, __func__, devt);
		goto err_get_dev_name;
	}

#ifdef CONFIG_SEC_SYSFS
	data->fac_dev = sec_device_create(data, dev_name);
#elif defined(CONFIG_DRV_SAMSUNG)
	data->fac_dev = sec_device_create(devt, data, dev_name);
#else
	data->fac_dev = device_create(sec_class, NULL, devt, data, dev_name);
#endif
	if (IS_ERR(data->fac_dev)) {
		pr_err("%s %s: failed to create device for the sysfs\n",
			SECLOG, __func__);
		goto err_sysfs_device;
	}

	dev_set_drvdata(data->fac_dev, data);
	ret = sysfs_create_group(&data->fac_dev->kobj, &sec_fac_attr_group);
	if (ret < 0) {
		pr_err("%s %s: failed to create sysfs group\n", SECLOG, __func__);
		goto err_sysfs_group;
	}

	return 0;

err_sysfs_group:
#ifdef CONFIG_SEC_SYSFS
	sec_device_destroy(data->fac_dev->devt);
#endif
err_sysfs_device:
err_get_dev_name:
#ifdef USE_SEC_CMD_QUEUE
	mutex_destroy(&data->fifo_lock);
	kfifo_free(&data->cmd_queue);
err_alloc_queue:
#endif
	kfree(data->cmd_result);
err_alloc_cmd_result:
	mutex_destroy(&data->cmd_lock);
	list_del(&data->cmd_list_head);
	return -ENODEV;
}

void sec_cmd_exit(struct sec_cmd_data *data, int devt)
{
#ifdef USE_SEC_CMD_QUEUE
	struct command cmd = {{0}};
	int ret;
#endif

	pr_info("%s %s", SECLOG, __func__);
	sysfs_remove_group(&data->fac_dev->kobj, &sec_fac_attr_group);
	dev_set_drvdata(data->fac_dev, NULL);
#ifdef CONFIG_SEC_SYSFS
	sec_device_destroy(data->fac_dev->devt);
#endif
#ifdef USE_SEC_CMD_QUEUE
	mutex_lock(&data->fifo_lock);
	while (kfifo_len(&data->cmd_queue)) {
		ret = kfifo_out(&data->cmd_queue, &cmd, sizeof(struct command));
		if (!ret)
			pr_err("%s %s: kfifo_out failed, it seems empty, ret=%d\n",
				SECLOG, __func__, ret);
		pr_info("%s %s: remove pending commands: %s", SECLOG, __func__,
			cmd.cmd);
	}
	mutex_unlock(&data->fifo_lock);
	mutex_destroy(&data->fifo_lock);
	kfifo_free(&data->cmd_queue);
	cancel_delayed_work_sync(&data->cmd_work);
	flush_delayed_work(&data->cmd_work);
#endif
	kfree(data->cmd_result);
	mutex_destroy(&data->cmd_lock);
	list_del(&data->cmd_list_head);
}

MODULE_DESCRIPTION("Samsung factory command");
MODULE_LICENSE("GPL");
