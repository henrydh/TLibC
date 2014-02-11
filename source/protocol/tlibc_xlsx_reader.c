#include "tlibc/protocol/tlibc_xlsx_reader.h"
#include "tlibc/core/tlibc_util.h"
#include "tlibc/protocol/tlibc_abstract_reader.h"
#include "tlibc/core/tlibc_error_code.h"
#include "tlibc/protocol/tlibc_xml_reader.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>

const char* tlibc_xlsx_reader_workbook_rels_search_file(tlibc_xlsx_reader_t *self, const char* rid);
const char* tlibc_xlsx_reader_workbook_search_rid(tlibc_xlsx_reader_t *self, const char* sheet);
void tlibc_xlsx_reader_load_sharedstring(tlibc_xlsx_reader_t *self);
TLIBC_ERROR_CODE tlibc_xlsx_reader_loadsheet(tlibc_xlsx_reader_t *self, tuint32 bindinfo_row, tuint32 data_row);

TLIBC_ERROR_CODE tlibc_xlsx_reader_init(tlibc_xlsx_reader_t *self, const char *file_name)
{
	TLIBC_ERROR_CODE ret = E_TLIBC_NOERROR;

	ret = tlibc_unzip_init(&self->unzip, file_name);
	if(ret != E_TLIBC_NOERROR)
	{
		goto ERROR_RET;
	}

	//load workbook
	ret = tlibc_unzip_locate(&self->unzip, "xl/workbook.xml");
	if(ret != E_TLIBC_NOERROR)
	{
		goto fini_unzip;
	}
	ret = tlibc_unzip_open_current_file(&self->unzip);
	if(ret != E_TLIBC_NOERROR)
	{
		goto fini_unzip;
	}
	self->workbook_buff_size = self->unzip.cur_file_info.uncompressed_size;
	self->workbook_buff = (char*)malloc(self->workbook_buff_size);
	ret = tlibc_read_current_file(&self->unzip, self->workbook_buff, &self->workbook_buff_size);
	if(ret != E_TLIBC_NOERROR)
	{
		tlibc_unzip_close_current_file (&self->unzip);
		goto free_workbook;
	}
	ret = tlibc_unzip_close_current_file (&self->unzip);
	if(ret != E_TLIBC_NOERROR)
	{
		goto free_workbook;
	}

	//load workbook rels
	ret = tlibc_unzip_locate(&self->unzip, "xl/_rels/workbook.xml.rels");
	if(ret != E_TLIBC_NOERROR)
	{
		goto free_workbook;
	}
	ret = tlibc_unzip_open_current_file(&self->unzip);
	if(ret != E_TLIBC_NOERROR)
	{
		goto free_workbook;
	}
	self->workbook_rels_buff_size = self->unzip.cur_file_info.uncompressed_size;
	self->workbook_rels_buff = (char*)malloc(self->workbook_rels_buff_size);
	ret = tlibc_read_current_file(&self->unzip, self->workbook_rels_buff, &self->workbook_rels_buff_size);
	if(ret != E_TLIBC_NOERROR)
	{
		tlibc_unzip_close_current_file (&self->unzip);
		goto free_workbook_rels;
	}
	ret = tlibc_unzip_close_current_file (&self->unzip);
	if(ret != E_TLIBC_NOERROR)
	{
		goto free_sharedstring;
	}


	//load shared string
	ret = tlibc_unzip_locate(&self->unzip, "xl/sharedStrings.xml");
	if(ret != E_TLIBC_NOERROR)
	{
		goto free_workbook_rels;
	}
	ret = tlibc_unzip_open_current_file(&self->unzip);
	if(ret != E_TLIBC_NOERROR)
	{
		goto free_workbook_rels;
	}
	self->sharedstring_buff_size = self->unzip.cur_file_info.uncompressed_size;
	self->sharedstring_buff = (char*)malloc(self->sharedstring_buff_size);	
	ret = tlibc_read_current_file(&self->unzip, self->sharedstring_buff, &self->sharedstring_buff_size);
	if(ret != E_TLIBC_NOERROR)
	{
		tlibc_unzip_close_current_file (&self->unzip);
		goto free_sharedstring;
	}
	ret = tlibc_unzip_close_current_file (&self->unzip);
	if(ret != E_TLIBC_NOERROR)
	{
		goto free_sharedstring;
	}
	tlibc_xlsx_reader_load_sharedstring(self);

	tlibc_abstract_reader_init(&self->super);

	self->super.read_vector_begin = tlibc_xlsx_read_vector_begin;
	self->super.read_vector_element_begin = tlibc_xlsx_read_vector_element_begin;
	self->super.read_vector_element_end = tlibc_xlsx_read_vector_element_end;
	self->super.read_field_begin = tlibc_xlsx_read_field_begin;	
	self->super.read_field_end = tlibc_xlsx_read_field_end;

	self->super.read_tint8 = tlibc_xlsx_read_tint8;
	self->super.read_tint16 = tlibc_xlsx_read_tint16;
	self->super.read_tint32 = tlibc_xlsx_read_tint32;
	self->super.read_tint64 = tlibc_xlsx_read_tint64;

	self->super.read_tuint8 = tlibc_xlsx_read_tuint8;
	self->super.read_tuint16 = tlibc_xlsx_read_tuint16;
	self->super.read_tuint32 = tlibc_xlsx_read_tuint32;
	self->super.read_tuint64 = tlibc_xlsx_read_tuint64;

	self->super.read_tdouble = tlibc_xlsx_read_tdouble;
	self->super.read_tchar = tlibc_xlsx_read_tchar;
	self->super.read_tstring = tlibc_xlsx_read_tstring;

	
	tlibc_hash_init(&self->name2index, self->hash_bucket, TLIBC_XLSX_HASH_BUCKET);
	

	return E_TLIBC_NOERROR;
free_sharedstring:
	free(self->sharedstring_buff);
free_workbook_rels:
	free(self->workbook_rels_buff);
free_workbook:
	free(self->workbook_buff);
fini_unzip:
	tlibc_unzip_fini(&self->unzip);
ERROR_RET:
	return E_TLIBC_ERROR;
}

#define XL_PREFIX "xl/"
TLIBC_API TLIBC_ERROR_CODE tlibc_xlsx_reader_open_sheet(tlibc_xlsx_reader_t *self, 
														const char* sheet, tuint32 bindinfo_row, tuint32 data_row)
{
	size_t i ;
	TLIBC_ERROR_CODE ret;
	const char *rid;
	const char *file;
	char sheet_file[TLIBC_MAX_PATH_LENGTH] = {XL_PREFIX};

	rid = tlibc_xlsx_reader_workbook_search_rid(self, sheet);
	if(rid == NULL)
	{
		goto ERROR_RET;
	}

	file = tlibc_xlsx_reader_workbook_rels_search_file(self, rid);
	strncpy(sheet_file + strlen(XL_PREFIX), file, TLIBC_MAX_PATH_LENGTH - strlen(XL_PREFIX) - 1);

	ret = tlibc_unzip_locate(&self->unzip, sheet_file);
	if(ret != E_TLIBC_NOERROR)
	{
		goto ERROR_RET;
	}

	ret = tlibc_unzip_open_current_file(&self->unzip);
	if(ret != E_TLIBC_NOERROR)
	{
		goto ERROR_RET;
	}
	self->sheet_buff_size = self->unzip.cur_file_info.uncompressed_size;
	self->sheet_buff = (char*)malloc(self->sheet_buff_size);	
	ret = tlibc_read_current_file(&self->unzip, self->sheet_buff, &self->sheet_buff_size);
	if(ret != E_TLIBC_NOERROR)
	{
		tlibc_unzip_close_current_file (&self->unzip);
		goto free_sheet;
	}
	ret = tlibc_unzip_close_current_file (&self->unzip);
	if(ret != E_TLIBC_NOERROR)
	{
		goto free_sheet;
	}
	if(tlibc_xlsx_reader_loadsheet(self, bindinfo_row, data_row) != E_TLIBC_NOERROR)
	{
		goto free_sheet;
	}

	self->curr_name_ptr = self->curr_name;
	self->read_vector_size = FALSE;
	self->skip_a_field = FALSE;
	self->curr_cell = NULL;
	self->ignore_int32_once = FALSE;

	for(i = 0; i < self->cell_col_size; ++i)
	{
		if(!self->bindinfo_row[i].empty)
		{
			tlibc_hash_insert(&self->name2index, self->bindinfo_row[i].val_begin
				, self->bindinfo_row[i].val_end - self->bindinfo_row[i].val_begin, &self->bindinfo_row[i].name2index);
		}
	}

	return E_TLIBC_NOERROR;
free_sheet:
	free(self->sheet_buff);
ERROR_RET:
	return E_TLIBC_ERROR;
}

tuint32 tlibc_xlsx_reader_num_rows(tlibc_xlsx_reader_t *self)
{
	return self->real_row_size - self->data_real_row_index;
}

void tlibc_xlsx_reader_row_seek(tlibc_xlsx_reader_t *self, tuint32 offset)
{
	self->curr_row = self->data_row + offset * self->cell_col_size;
}

void tlibc_xlsx_reader_close_sheet(tlibc_xlsx_reader_t *self)
{
	free(self->sheet_buff);
	tlibc_hash_clear(&self->name2index);
}

void tlibc_xlsx_reader_fini(tlibc_xlsx_reader_t *self)
{
	free(self->workbook_buff);
	free(self->workbook_rels_buff);
	free(self->sharedstring_buff);
	
	tlibc_unzip_fini(&self->unzip);
}


TLIBC_ERROR_CODE tlibc_xlsx_read_vector_begin(TLIBC_ABSTRACT_READER *super)
{
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	
	self->skip_a_field = TRUE;
	self->read_vector_size = TRUE;

	return E_TLIBC_NOERROR;
}

static void tlibc_xlsx_locate(tlibc_xlsx_reader_t *self)
{
	tlibc_xlsx_cell_s *cell;
	tlibc_hash_head_t *head;

	self->curr_cell = NULL;
	if(self->curr_name_ptr <= self->curr_name)
	{		
		goto done;
	}
	head = tlibc_hash_find(&self->name2index, self->curr_name + 1, self->curr_name_ptr - self->curr_name - 1);
	if(head == NULL)
	{
		goto done;
	}
	cell = TLIBC_CONTAINER_OF(head, tlibc_xlsx_cell_s, name2index);
	self->curr_cell = self->curr_row + (cell - self->bindinfo_row);

done:
	return;
}
TLIBC_ERROR_CODE tlibc_xlsx_read_vector_element_begin(TLIBC_ABSTRACT_READER *super, const char* var_name, tuint32 index)
{
	int len;
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	
	len = snprintf(self->curr_name_ptr, TLIBC_XLSX_READER_NAME_LENGTH - (self->curr_name_ptr - self->curr_name)
		, ".%s[%u]", var_name, index);

	if(len < 0)
	{
		goto ERROR_RET;
	}
	self->curr_name_ptr += len;

	tlibc_xlsx_locate(self);

	return E_TLIBC_NOERROR;
ERROR_RET:
	return E_TLIBC_ERROR;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_vector_element_end(TLIBC_ABSTRACT_READER *super, const char* var_name, tuint32 index)
{
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	int len;
	char curr_name[TLIBC_XLSX_READER_NAME_LENGTH];
	
	len = snprintf(curr_name, TLIBC_XLSX_READER_NAME_LENGTH, ".%s[%u]", var_name, index);
	if(len < 0)
	{
		goto ERROR_RET;
	}
	self->curr_name_ptr -= len;

	return E_TLIBC_NOERROR;
ERROR_RET:
	return E_TLIBC_ERROR;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_field_begin(TLIBC_ABSTRACT_READER *super, const char *var_name)
{
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	int len;
	if(self->skip_a_field)
	{
		goto done;
	}

	len = snprintf(self->curr_name_ptr, TLIBC_XLSX_READER_NAME_LENGTH - (self->curr_name_ptr - self->curr_name)
		, ".%s", var_name);
	if(len < 0)
	{
		goto ERROR_RET;
	}
	self->curr_name_ptr += len;

	tlibc_xlsx_locate(self);

done:
	return E_TLIBC_NOERROR;
ERROR_RET:
	return E_TLIBC_ERROR;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_field_end(TLIBC_ABSTRACT_READER *super, const char *var_name)
{
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	int len;
	char curr_name[TLIBC_XLSX_READER_NAME_LENGTH];	
	if(self->skip_a_field)
	{
		self->skip_a_field = FALSE;
		goto done;
	}

	len = snprintf(curr_name, TLIBC_XLSX_READER_NAME_LENGTH, ".%s", var_name);
	if(len < 0)
	{
		goto ERROR_RET;
	}
	self->curr_name_ptr -= len;

done:
	return E_TLIBC_NOERROR;
ERROR_RET:
	return E_TLIBC_ERROR;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_enum_begin(TLIBC_ABSTRACT_READER *super, const char *enum_name)
{
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	TLIBC_UNUSED(enum_name);

	self->ignore_int32_once = TRUE;

	return E_TLIBC_NOERROR;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tint8(TLIBC_ABSTRACT_READER *super, tint8 *val)
{
	tint64 i64;
	TLIBC_ERROR_CODE ret = tlibc_xlsx_read_tint64(super, &i64);
	*val = (tint8)i64;
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tint16(TLIBC_ABSTRACT_READER *super, tint16 *val)
{
	tint64 i64;
	TLIBC_ERROR_CODE ret = tlibc_xlsx_read_tint64(super, &i64);
	*val = (tint16)i64;
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tint32(TLIBC_ABSTRACT_READER *super, tint32 *val)
{
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	tint64 i64;
	TLIBC_ERROR_CODE ret;
	if(self->ignore_int32_once)
	{
		self->ignore_int32_once = FALSE;
		ret = E_TLIBC_IGNORE;
		goto done;
	}
	ret = tlibc_xlsx_read_tint64(super, &i64);
	*val = (tint32)i64;
done:
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tint64(TLIBC_ABSTRACT_READER *super, tint64 *val)
{
	TLIBC_ERROR_CODE ret;
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	if(self->curr_cell == NULL)
	{
		ret = E_TLIBC_CONVERT_ERROR;
		goto ERROR_RET;
	}
	errno = 0;
	*val = strtoll(self->curr_cell->val_begin, NULL, 10);
	if(errno != 0)
	{
		ret = E_TLIBC_CONVERT_ERROR;
		goto ERROR_RET;
	}

	return E_TLIBC_NOERROR;
ERROR_RET:
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tuint8(TLIBC_ABSTRACT_READER *super, tuint8 *val)
{
	tint64 i64;
	TLIBC_ERROR_CODE ret = tlibc_xlsx_read_tint64(super, &i64);
	*val = (tuint8)i64;
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tuint16(TLIBC_ABSTRACT_READER *super, tuint16 *val)
{
	tint64 i64;
	TLIBC_ERROR_CODE ret = tlibc_xlsx_read_tint64(super, &i64);
	*val = (tuint16)i64;
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tuint32(TLIBC_ABSTRACT_READER *super, tuint32 *val)
{
	TLIBC_ERROR_CODE ret = E_TLIBC_NOERROR;
	tint64 i64;
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	if(self->read_vector_size)
	{
		self->read_vector_size = FALSE;
		*val = self->real_row_size - self->data_real_row_index;
		goto done;
	}
	ret = tlibc_xlsx_read_tint64(super, &i64);
	*val = (tuint32)i64;
done:
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tuint64(TLIBC_ABSTRACT_READER *super, tuint64 *val)
{
	TLIBC_ERROR_CODE ret;
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	if(self->curr_cell == NULL)
	{
		ret = E_TLIBC_CONVERT_ERROR;
		goto ERROR_RET;
	}

	errno = 0;
	*val = strtoull(self->curr_cell->val_begin, NULL, 10);
	if(errno != 0)
	{
		ret = E_TLIBC_CONVERT_ERROR;
		goto ERROR_RET;
	}

	return E_TLIBC_NOERROR;
ERROR_RET:
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tdouble(TLIBC_ABSTRACT_READER *super, double *val)
{
	TLIBC_ERROR_CODE ret;
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	if(self->curr_cell == NULL)
	{
		ret = E_TLIBC_CONVERT_ERROR;
		goto ERROR_RET;
	}

	errno = 0;
	*val = strtod(self->curr_cell->val_begin, NULL);
	if(errno != 0)
	{
		ret = E_TLIBC_CONVERT_ERROR;
		goto ERROR_RET;
	}

	return E_TLIBC_NOERROR;
ERROR_RET:
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tchar(TLIBC_ABSTRACT_READER *super, char *val)
{
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	const char* curr;
	TLIBC_ERROR_CODE ret = E_TLIBC_NOERROR;
	if(self->curr_cell == NULL)
	{
		ret = E_TLIBC_CONVERT_ERROR;
		goto done;
	}

	curr = tlibc_xml_str2c(self->curr_cell->val_begin
		, self->curr_cell->val_end, val);
	if(curr == NULL)
	{
		return E_TLIBC_OUT_OF_MEMORY;
	}
done:
	return ret;
}

TLIBC_ERROR_CODE tlibc_xlsx_read_tstring(TLIBC_ABSTRACT_READER *super, tchar *str, tuint32 str_len)
{
	tlibc_xlsx_reader_t *self = TLIBC_CONTAINER_OF(super, tlibc_xlsx_reader_t, super);
	tuint32 len = 0;
	const char* curr;
	const char* limit;
	TLIBC_ERROR_CODE ret = E_TLIBC_NOERROR;
	if(self->curr_cell == NULL)
	{
		ret = E_TLIBC_CONVERT_ERROR;
		goto done;
	}
	curr = self->curr_cell->val_begin;
	limit = self->curr_cell->val_end;
	while(curr < limit)
	{
		char c;
		curr = tlibc_xml_str2c(curr, limit, &c);
		if(curr == NULL)
		{
			ret = E_TLIBC_OUT_OF_MEMORY;
			goto done;
		}

		if(c == '<')
		{
			break;
		}
		if(len >= str_len)
		{
			ret = E_TLIBC_OUT_OF_MEMORY;
			goto done;
		}
		str[len++] = c;		
	}
	str[len] = 0;
done:
	return ret;
}
