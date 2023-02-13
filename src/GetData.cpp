#include "main.hpp"

#include "PythonInternal.hpp"
#include "Utils/FileUtils.hpp"
#include "Utils/StringUtils.hpp"

if (search.EndsWith("_Type") && !search.Equals("PyObject_Type"))
                {

                    declares.Add("DECLARE_DLSYM_TYPE(" + search + ");");
                    loads.Add("LOAD_DLSYM_TYPE(libpython, " + search + ");");
                    continue;
                }
                string currentBuffer = null;
                foreach (string file in allFiles)
                {
                    foreach (string line in File.ReadAllLines(file))
                    {
                        if (line.StartsWith("PyAPI_FUNC(") && line.Contains(search + "("))
                        {
                            //DECLARE_DLSYM(PyObject*, PyObject_CallObject, PyObject* callable, PyObject* args);
                            var start = "PyAPI_FUNC(".Length;
                            var end = line.IndexOf(")");
                            currentBuffer = ("DECLARE_DLSYM(" + line.Substring(start, end - start) + ", " + search + ", " + line.Substring(line.IndexOf(search) + search.Length + 1)).Trim();
                            if (currentBuffer.Contains(");"))
                                break;
                        }
                        else
                        if (currentBuffer != null)
                        {
                            currentBuffer = currentBuffer + line.Trim();
                            if (currentBuffer.Contains(");"))
                                break;
                        }
                    }
                    if (currentBuffer != null)
                        break;
                }
                if (currentBuffer != null && currentBuffer.EndsWith(");"))
                {
                    if (currentBuffer.Contains("Py_GCC_ATTRIBUTE"))
                        continue;
                    Regex regex = new Regex(string.Format("Py.+?\\*"), RegexOptions.RightToLeft);
                    var currentTypes = regex.Matches(currentBuffer).Where(x =>
                    {
                        int index = x.Value.IndexOf(",");
                        if (index == -1)
                            return true;
                        return x.Value.Substring(0, index).Contains("*");
                    }
                    ).Select(x => "struct " + x.Value.Substring(0, x.Value.IndexOf("*")).Trim() + " { };").Where(x => !x.Contains("/")).ToList();
                    types.AddRange(currentTypes.Except(types));
                    Console.WriteLine(currentBuffer);
                    declares.Add(currentBuffer);
                    loads.Add("LOAD_DLSYM(libpython, " + search + ");");
                }
            }
            File.WriteAllLines(declare, types.Concat(declares));
            File.WriteAllLines(load, loads);
        }
    }
}
