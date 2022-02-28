
##Instructions on how to run this program
## python3 analysis.py
import sys

INPUT_FILE_PATH = ["/u/csc369h/summer/pub/a3/traces/addr-blocked.ref", "/u/csc369h/summer/pub/a3/traces/addr-matmul.ref", "/u/csc369h/summer/pub/a3/traces/addr-repeatloop.ref"
    , "/u/csc369h/summer/pub/a3/traces/addr-simpleloop.ref"]
OUTPUT_FILE_PATH = "./analysis.txt"

def analysis(f):

    lines = f.readlines()
    count_dic = {"I": 0, "L": 0, "S": 0, "M": 0}
    instruction_dic = {}
    data_dic = {}
    for line in lines:
        split_line = line.split()
        access = split_line[0]
        addr = split_line[1].split(",")[0]
        count_dic[access] += 1
        page = addr[:-3] + "000"
        if access == "I":
            if page not in instruction_dic:
                instruction_dic[page] = 0
            instruction_dic[page] += 1
        else:
            if page not in data_dic:
                data_dic[page] = 0
            data_dic[page] += 1
    print("Counts:")

    sort_orders = sorted(count_dic.items(), key=lambda x: x[1], reverse=True)

    for i in sort_orders:
        if i[0] == "I":
            print(" Instructions %s" % (i[1]))
        elif i[0] == "L":
            print(" Loads        %s" % (i[1]))
        elif i[0] == "S":
            print(" Stores       %s" % (i[1]))
        elif i[0] == "M":
            print(" Modifies     %s" % (i[1]))
    print("\nInstructions:")
    sort_orders = sorted(instruction_dic.items(), key=lambda x: x[1], reverse=True)
    j = 0
    for i in sort_orders:
        print("0x%s,%d" % (i[0].lstrip("0"), i[1]))
        j += 1
        if j == 15:
            break

    print("Data:")
    sort_orders = sorted(data_dic.items(), key=lambda x: x[1],
                         reverse=True)
    j = 0
    for i in sort_orders:
        print("0x%s,%d" % (i[0].lstrip("0"), i[1]))
        j += 1
        if j == 15:
            break
    print("unique pages: %d" % (len(instruction_dic)+len(data_dic)))
    f.close()









# Press the green button in the gutter to run the script.
if __name__ == '__main__':
    #if len(sys.argv) != 3:
     #   print("Usage: python3 Analysis.py <inputfilename> <outputfilename>")
    #    sys.exit()

    print("program starts, waiting ...")

    file_out = open("analysis.txt", "w")
    org_stdout = sys.stdout
    sys.stdout = file_out
    for file in INPUT_FILE_PATH:
        file_in = open(file, "r")
        print("=== Traces for %s ===" % file)
        analysis(file_in)
    sys.stdout = org_stdout
    file_out.close()
    print("program finishes, please see output in %s" % OUTPUT_FILE_PATH)

# See PyCharm help at https://www.jetbrains.com/help/pycharm/
