"""What the templates need to draw a form with Bootstrap: `{{ field|bs }}`
renders a form's field with the class its kind takes (form-control,
form-select, form-check-input) and marks one with errors invalid. A template
library of its own ({% load portal_ui %}, settings.TEMPLATES), so no form -
the portal's or Django's own sign-in and password forms - has to know how
the pages look.

Version: 0.3.0
"""

from __future__ import annotations

from django import template

register = template.Library()

CHECKS = {"checkbox", "radio"}
SELECTS = {"select", "selectmultiple", "nullbooleanselect"}


@register.filter
def bs(field):
    """The bound field's widget with Bootstrap's classes."""
    kind = field.widget_type
    css = "form-check-input" if kind in CHECKS else "form-select" if kind in SELECTS else "form-control"
    if field.errors:
        css += " is-invalid"
    own = field.field.widget.attrs.get("class", "")
    return field.as_widget(attrs={"class": f"{own} {css}".strip()})
