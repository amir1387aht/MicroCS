/* Generated native pointer/height and disabled-font checks for every declared built-in. */
run(vm, "var fontsGui=new LvGui(80,60); var fontsObj=fontsGui.Screen.Create(\"label\"); fontsObj.Text=\"Aa 123\";");
#if LVGL_VERSION_MAJOR == 9
lv_obj_t *font_obj = lv_obj_get_child(lv_screen_active(), 0);
#else
lv_obj_t *font_obj = lv_obj_get_child(lv_scr_act(), 0);
#endif
CHECK(font_obj != NULL);
run(vm, "fontsObj.SetFont(\"Default\"); if(fontsObj.FontHeight<=0) throw new Exception(\"default height\");");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == LV_FONT_DEFAULT);
#if LV_FONT_DEJAVU_16_PERSIAN_HEBREW
run(vm, "if(!LvGui.HasFont(\"lv_font_dejavu_16_persian_hebrew\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_dejavu_16_persian_hebrew\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_dejavu_16_persian_hebrew);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_dejavu_16_persian_hebrew)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_dejavu_16_persian_hebrew\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_dejavu_16_persian_hebrew\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_10
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_10\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_10\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_10);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_10)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_10\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_10\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_12
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_12\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_12\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_12);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_12)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_12\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_12\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LVGL_VERSION_MAJOR == 8 && LV_FONT_MONTSERRAT_12_SUBPX
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_12_subpx\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_12_subpx\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_12_subpx);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_12_subpx)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_12_subpx\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_12_subpx\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_14
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_14\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_14\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_14);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_14)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_14\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_14\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_16
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_16\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_16\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_16);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_16)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_16\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_16\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_18
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_18\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_18\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_18);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_18)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_18\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_18\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_20
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_20\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_20\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_20);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_20)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_20\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_20\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_22
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_22\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_22\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_22);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_22)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_22\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_22\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_24
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_24\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_24\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_24);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_24)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_24\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_24\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_26
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_26\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_26\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_26);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_26)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_26\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_26\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_28
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_28\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_28\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_28);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_28)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_28\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_28\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_28_COMPRESSED
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_28_compressed\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_28_compressed\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_28_compressed);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_28_compressed)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_28_compressed\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_28_compressed\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_30
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_30\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_30\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_30);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_30)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_30\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_30\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_32
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_32\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_32\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_32);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_32)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_32\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_32\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_34
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_34\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_34\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_34);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_34)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_34\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_34\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_36
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_36\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_36\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_36);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_36)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_36\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_36\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_38
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_38\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_38\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_38);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_38)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_38\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_38\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_40
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_40\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_40\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_40);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_40)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_40\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_40\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_42
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_42\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_42\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_42);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_42)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_42\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_42\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_44
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_44\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_44\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_44);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_44)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_44\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_44\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_46
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_46\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_46\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_46);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_46)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_46\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_46\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_48
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_48\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_48\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_48);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_48)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_48\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_48\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_MONTSERRAT_8
run(vm, "if(!LvGui.HasFont(\"lv_font_montserrat_8\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_montserrat_8\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_montserrat_8);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_montserrat_8)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_montserrat_8\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_montserrat_8\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LVGL_VERSION_MAJOR == 8 && LV_FONT_SIMSUN_16_CJK
run(vm, "if(!LvGui.HasFont(\"lv_font_simsun_16_cjk\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_simsun_16_cjk\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_simsun_16_cjk);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_simsun_16_cjk)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_simsun_16_cjk\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_simsun_16_cjk\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LVGL_VERSION_MAJOR == 9 && LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
run(vm, "if(!LvGui.HasFont(\"lv_font_source_han_sans_sc_14_cjk\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_source_han_sans_sc_14_cjk\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_source_han_sans_sc_14_cjk);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_source_han_sans_sc_14_cjk)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_source_han_sans_sc_14_cjk\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_source_han_sans_sc_14_cjk\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LVGL_VERSION_MAJOR == 9 && LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
run(vm, "if(!LvGui.HasFont(\"lv_font_source_han_sans_sc_16_cjk\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_source_han_sans_sc_16_cjk\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_source_han_sans_sc_16_cjk);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_source_han_sans_sc_16_cjk)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_source_han_sans_sc_16_cjk\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_source_han_sans_sc_16_cjk\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_UNSCII_16
run(vm, "if(!LvGui.HasFont(\"lv_font_unscii_16\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_unscii_16\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_unscii_16);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_unscii_16)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_unscii_16\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_unscii_16\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
#if LV_FONT_UNSCII_8
run(vm, "if(!LvGui.HasFont(\"lv_font_unscii_8\")) throw new Exception(\"enabled font absent\"); fontsObj.SetFont(\"lv_font_unscii_8\"); fontsGui.Pump(40);");
CHECK(lv_obj_get_style_text_font(font_obj, LV_PART_MAIN) == &lv_font_unscii_8);
{ char check_height[160]; snprintf(check_height, sizeof check_height, "if(fontsObj.FontHeight != %d) throw new Exception(\"font height\");", (int)lv_font_get_line_height(&lv_font_unscii_8)); run(vm, check_height); }
#else
run(vm, "if(LvGui.HasFont(\"lv_font_unscii_8\")) throw new Exception(\"disabled font present\"); try { fontsObj.SetFont(\"lv_font_unscii_8\"); throw new Exception(\"disabled font accepted\"); } catch(NotSupportedException e) {}");
#endif
run(vm, "fontsObj.SetFont(\"Default\"); fontsGui.Dispose();");
