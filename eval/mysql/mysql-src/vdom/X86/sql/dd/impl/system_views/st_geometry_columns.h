/* Copyright (c) 2017, 2021, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef DD_SYSTEM_VIEWS__ST_GEOMETRY_COLUMNS_INCLUDED
#define DD_SYSTEM_VIEWS__ST_GEOMETRY_COLUMNS_INCLUDED

#include "sql/dd/impl/system_views/system_view_definition_impl.h"
#include "sql/dd/impl/system_views/system_view_impl.h"
#include "sql/dd/string_type.h"

namespace dd {
namespace system_views {

/*
  The class representing INFORMATION_SCHEMA.ST_GEOMETRY_COLUMNS system view
  definition.
*/
class St_geometry_columns
    : public System_view_impl<System_view_select_definition_impl> {
 public:
  enum enum_fields {
    FIELD_TABLE_CATALOG,
    FIELD_TABLE_SCHEMA,
    FIELD_TABLE_NAME,
    FIELD_COLUMN_NAME,
    FIELD_SRS_NAME,
    FIELD_SRS_ID,
    FIELD_GEOMETRY_TYPE_NAME
  };

  St_geometry_columns();

  static const St_geometry_columns &instance();

  static const String_type &view_name() {
    static String_type s_view_name("ST_GEOMETRY_COLUMNS");
    return s_view_name;
  }

  const String_type &name() const override {
    return St_geometry_columns::view_name();
  }
};

}  // namespace system_views
}  // namespace dd

#endif  // DD_SYSTEM_VIEWS__ST_GEOMETRY_COLUMNS_INCLUDED